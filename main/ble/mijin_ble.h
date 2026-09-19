#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>

#include "host/ble_hs.h"

/* ==================== mijin 离线通道 BLE 服务 ====================
 *
 * GATT 服务 UUID: 0xFFE0（mijin robot）
 *   0xFFE1 指令通道  (Write)     二进制帧：cmd + 载荷
 *   0xFFE2 音频通道  (WriteNoRsp) 16kHz/16bit 单声道 PCM 流
 *   0xFFE3 状态通道  (Notify)     JSON 状态上报（电量/设备状态）
 *   0xFFE4 握手通道  (Read/Write) 协议版本/能力协商
 *
 * 指令集（与需求文档 §5 协议一致）：
 *   0x01 前进 / 0x02 后退 / 0x03 左转 / 0x04 右转 / 0x05 停止
 *   0x06 加速 / 0x07 减速 / 0x08 自动巡航 / 0x09 跟随(预留)
 *   0x10 表情(+1B 表情ID) / 0x11 音频开始 / 0x12 音频结束
 *   0x13 查询状态 / 0x14 设置速度(+1B 0~100) / 0x15 查询版本
 */

namespace mijin {

enum class Cmd : uint8_t {
    kMoveForward  = 0x01,
    kMoveBackward = 0x02,
    kTurnLeft     = 0x03,
    kTurnRight    = 0x04,
    kStop         = 0x05,
    kAccelerate   = 0x06,
    kDecelerate   = 0x07,
    kCruise       = 0x08,
    kFollow       = 0x09,   // 预留（需传感器）
    kEmotion      = 0x10,   // +1B 表情ID（NIMBO_EMOTION_*）
    kAudioStart   = 0x11,
    kAudioStop    = 0x12,
    kGetStatus    = 0x13,
    kSpeedSet     = 0x14,   // +1B 0~100
    kVersion      = 0x15,
};

struct BleCallbacks {
    std::function<void(const uint8_t* data, size_t len)> on_command;  // 指令帧（含 cmd 字节）
    std::function<void(const uint8_t* data, size_t len)> on_audio;    // PCM 数据
    std::function<void(bool connected)> on_conn_state;                // 连接状态变化
};

class MijinBle {
public:
    static MijinBle& GetInstance();

    /** 初始化 NimBLE host + GATT（需在 NVS 初始化之后调用） */
    void Init(const BleCallbacks& cbs);

    /** 启动广播 */
    void Start();

    /** 当前是否有手机连接 */
    bool IsConnected() const { return connected_; }

    /** 经 0xFFE3 状态通道上报 JSON 文本 */
    void Notify(const char* json);

    /* NimBLE 回调（GATT 表与 GAP 事件需在类外引用，故置 public） */
    static int  OnGapEvent(struct ble_gap_event* event, void* arg);
    static int  OnGattAccess(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt* ctxt, void* arg);

private:
    MijinBle() = default;
    ~MijinBle() = default;
    MijinBle(const MijinBle&) = delete;
    MijinBle& operator=(const MijinBle&) = delete;

    static MijinBle& Self(void* arg);
    static void OnHostSync(void);
    static void OnHostReset(int reason);
    static void HostTask(void* param);
    static void DispatchTask(void* param);

    BleCallbacks cbs_;
    volatile bool connected_ = false;
    uint16_t conn_handle_ = BLE_HS_CONN_HANDLE_NONE;
};

}  // namespace mijin
