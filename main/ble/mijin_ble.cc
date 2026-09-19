#include "ble/mijin_ble.h"

#include <esp_log.h>
#include <nvs_flash.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <host/ble_hs_adv.h>
#include <host/ble_gap.h>
#include <host/util/util.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

/* ESP-IDF NimBLE 的配置存储入口（头文件未暴露，按官方例程自行声明） */
extern "C" void ble_store_config_init(void);

#define TAG "MijinBle"

#define BLE_SVC_MIJIN_UUID16     0xFFE0
#define BLE_CHR_CMD_UUID16       0xFFE1
#define BLE_CHR_AUDIO_UUID16     0xFFE2
#define BLE_CHR_STATUS_UUID16    0xFFE3
#define BLE_CHR_HANDSHAKE_UUID16 0xFFE4

#define MIJIN_BLE_NAME           "MijinRobot"
#define MIJIN_BLE_VERSION        "mijin-ble-1.0"

/* 指令/音频分发队列：NimBLE 回调在 BT 任务上下文，重活全部抛到这里 */
#define MIJIN_BLE_DISPATCH_QUEUE_LEN 64
#define MIJIN_BLE_DISPATCH_STACK_SIZE (4096)

namespace mijin {

/* ==================== 特征 UUID ==================== */

int gatt_svr_init_static(const struct ble_gatt_svc_def* svcs);  // 前置声明

static const ble_uuid16_t s_cmd_uuid = BLE_UUID16_INIT(BLE_CHR_CMD_UUID16);
static const ble_uuid16_t s_audio_uuid = BLE_UUID16_INIT(BLE_CHR_AUDIO_UUID16);
static const ble_uuid16_t s_status_uuid = BLE_UUID16_INIT(BLE_CHR_STATUS_UUID16);
static const ble_uuid16_t s_handshake_uuid = BLE_UUID16_INIT(BLE_CHR_HANDSHAKE_UUID16);
static const ble_uuid16_t s_svc_uuid = BLE_UUID16_INIT(BLE_SVC_MIJIN_UUID16);

static uint16_t s_status_val_handle = 0;   /* 状态特征值句柄（Notify 用），注册时自动填充 */

/* ==================== GATT 定义 ==================== */

static struct ble_gatt_chr_def s_mijin_chrs[] = {
    { /* 指令通道：可靠写 */
        .uuid = (ble_uuid_t*)&s_cmd_uuid,
        .access_cb = MijinBle::OnGattAccess,
        .flags = BLE_GATT_CHR_F_WRITE,
        .val_handle = nullptr,
    },
    { /* 音频通道：WriteNoRsp 保证吞吐 */
        .uuid = (ble_uuid_t*)&s_audio_uuid,
        .access_cb = MijinBle::OnGattAccess,
        .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
        .val_handle = nullptr,
    },
    { /* 状态通道：Notify */
        .uuid = (ble_uuid_t*)&s_status_uuid,
        .access_cb = MijinBle::OnGattAccess,
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_status_val_handle,
    },
    { /* 握手通道：协议版本 */
        .uuid = (ble_uuid_t*)&s_handshake_uuid,
        .access_cb = MijinBle::OnGattAccess,
        .flags = BLE_GATT_CHR_F_READ,
        .val_handle = nullptr,
    },
    { 0, },
};

static const struct ble_gatt_svc_def s_mijin_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = (ble_uuid_t*)&s_svc_uuid,
        .characteristics = s_mijin_chrs,
    },
    { 0, },
};

/* ==================== 分发队列 ==================== */

namespace {

struct DispatchItem {
    uint8_t channel;          // 1=cmd 2=audio
    uint8_t* data;
    size_t len;
};

QueueHandle_t s_dispatch_queue = nullptr;

}  // namespace

/* ==================== 回调桥接 ==================== */

void MijinBle::OnHostSync(void) {
    MijinBle::GetInstance().Start();
}

void MijinBle::OnHostReset(int reason) {
    ESP_LOGE(TAG, "NimBLE host reset, reason=%d", reason);
}

MijinBle& MijinBle::Self(void* arg) {
    return *static_cast<MijinBle*>(arg);
}

void MijinBle::HostTask(void* param) {
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void MijinBle::DispatchTask(void* param) {
    (void)param;
    auto& ble = MijinBle::GetInstance();
    DispatchItem item;
    while (xQueueReceive(s_dispatch_queue, &item, portMAX_DELAY)) {
        if (item.channel == 1 && ble.cbs_.on_command) {
            ble.cbs_.on_command(item.data, item.len);
        } else if (item.channel == 2 && ble.cbs_.on_audio) {
            ble.cbs_.on_audio(item.data, item.len);
        }
        free(item.data);
    }
}

/* ==================== GAP 事件 ==================== */

int MijinBle::OnGapEvent(struct ble_gap_event* event, void* arg) {
    auto& ble = MijinBle::Self(arg);
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ble.connected_ = true;
            ble.conn_handle_ = event->connect.conn_handle;
            ESP_LOGI(TAG, "phone connected, conn_handle=%d", ble.conn_handle_);
            if (ble.cbs_.on_conn_state) {
                ble.cbs_.on_conn_state(true);
            }
        } else {
            ESP_LOGI(TAG, "connect failed, resume advertising");
            ble.Start();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "phone disconnected, reason=%d", event->disconnect.reason);
        ble.connected_ = false;
        ble.conn_handle_ = BLE_HS_CONN_HANDLE_NONE;
        if (ble.cbs_.on_conn_state) {
            ble.cbs_.on_conn_state(false);
        }
        ble.Start();  // 恢复广播
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "subscribe: attr_handle=%d notify=%d", event->subscribe.attr_handle,
                 event->subscribe.cur_notify);
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated: %d", event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

/* ==================== GATT 访问 ==================== */

int MijinBle::OnGattAccess(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt* ctxt, void* arg) {
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        if (ble_uuid_cmp(ctxt->chr->uuid, &s_handshake_uuid.u) == 0) {
            int rc = os_mbuf_append(ctxt->om, MIJIN_BLE_VERSION, sizeof(MIJIN_BLE_VERSION));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return 0;

    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len == 0) {
            return 0;
        }
        uint8_t* buf = (uint8_t*)malloc(len);
        if (!buf) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        uint16_t copied = 0;
        int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, len, &copied);
        if (rc != 0 || copied == 0) {
            free(buf);
            return BLE_ATT_ERR_UNLIKELY;
        }

        uint8_t channel = 0;
        if (ble_uuid_cmp(ctxt->chr->uuid, &s_cmd_uuid.u) == 0) {
            channel = 1;
        } else if (ble_uuid_cmp(ctxt->chr->uuid, &s_audio_uuid.u) == 0) {
            channel = 2;
        }

        if (channel != 0 && s_dispatch_queue) {
            DispatchItem item = {.channel = channel, .data = buf, .len = copied};
            if (xQueueSend(s_dispatch_queue, &item, 0) != pdPASS) {
                ESP_LOGW(TAG, "dispatch queue full, dropping %u bytes", copied);
                free(buf);
            }
        } else {
            free(buf);
        }
        return 0;
    }

    default:
        return 0;
    }
}

/* ==================== 广播 ==================== */

void MijinBle::Start() {
    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t*)MIJIN_BLE_NAME;
    fields.name_len = strlen(MIJIN_BLE_NAME);
    fields.name_is_complete = 1;
    fields.uuids16 = (ble_uuid16_t[]){BLE_UUID16_INIT(BLE_SVC_MIJIN_UUID16)};
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields failed: %d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params = {};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER, &adv_params,
                           OnGapEvent, this);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "adv_start failed: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "advertising as '%s'", MIJIN_BLE_NAME);
}

/* ==================== 对外接口 ==================== */

MijinBle& MijinBle::GetInstance() {
    static MijinBle instance;
    return instance;
}

void MijinBle::Init(const BleCallbacks& cbs) {
    cbs_ = cbs;
    conn_handle_ = BLE_HS_CONN_HANDLE_NONE;

    /* NVS：NimBLE 需要存储校准/绑定数据 */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", ret);
        return;
    }

    ble_hs_cfg.reset_cb = OnHostReset;
    ble_hs_cfg.sync_cb = OnHostSync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    /* 无配对（离线玩具级设备，BLE 不加密） */
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_sc = 0;

    int rc = ble_svc_gap_device_name_set(MIJIN_BLE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "device name set failed: %d", rc);
    }

    /* 注册 GATT 服务 */
    rc = gatt_svr_init_static(s_mijin_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatt_svr_init failed: %d", rc);
    }

    ble_store_config_init();

    /* 分发任务（指令/音频） */
    s_dispatch_queue = xQueueCreate(MIJIN_BLE_DISPATCH_QUEUE_LEN, sizeof(DispatchItem));
    xTaskCreate(DispatchTask, "mijin_ble_dispatch", MIJIN_BLE_DISPATCH_STACK_SIZE, nullptr, 5,
                nullptr);

    nimble_port_freertos_init(HostTask);
    ESP_LOGI(TAG, "NimBLE host started");
}

void MijinBle::Notify(const char* json) {
    if (!connected_ || conn_handle_ == BLE_HS_CONN_HANDLE_NONE || s_status_val_handle == 0) {
        return;
    }
    struct os_mbuf* om = ble_hs_mbuf_from_flat(json, strlen(json));
    if (!om) {
        return;
    }
    int rc = ble_gatts_notify_custom(conn_handle_, s_status_val_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "notify failed: %d", rc);
    }
}

/* ==================== GATT 注册（静态服务表） ==================== */

int gatt_svr_init_static(const struct ble_gatt_svc_def* svcs) {
    int rc;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(svcs);
    if (rc != 0) {
        return rc;
    }

    rc = ble_gatts_add_svcs(svcs);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

}  // namespace mijin
