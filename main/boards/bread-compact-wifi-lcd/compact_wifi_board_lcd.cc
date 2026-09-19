#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "motor_controller.h"
#include "ble/mijin_ble.h"
#include "audio/ble_audio_player.h"
#include "nimbo_display.h"
#include "display/lcd_display.h"

#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_system.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>

#if defined(LCD_TYPE_ILI9341_SERIAL)
#include "esp_lcd_ili9341.h"
#endif

#if defined(LCD_TYPE_GC9A01_SERIAL)
#include "esp_lcd_gc9a01.h"
static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb1, (uint8_t[]){0x80}, 1, 0},
    {0xb2, (uint8_t[]){0x27}, 1, 0},
    {0xb3, (uint8_t[]){0x13}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x05}, 1, 0},
    {0xac, (uint8_t[]){0xc8}, 1, 0},
    {0xab, (uint8_t[]){0x0f}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x08}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xea, (uint8_t[]){0x02}, 1, 0},
    {0xe8, (uint8_t[]){0x2A}, 1, 0},
    {0xe9, (uint8_t[]){0x47}, 1, 0},
    {0xe7, (uint8_t[]){0x5f}, 1, 0},
    {0xc6, (uint8_t[]){0x21}, 1, 0},
    {0xc7, (uint8_t[]){0x15}, 1, 0},
    {0xf0,
    (uint8_t[]){0x1D, 0x38, 0x09, 0x4D, 0x92, 0x2F, 0x35, 0x52, 0x1E, 0x0C,
                0x04, 0x12, 0x14, 0x1f},
    14, 0},
    {0xf1,
    (uint8_t[]){0x16, 0x40, 0x1C, 0x54, 0xA9, 0x2D, 0x2E, 0x56, 0x10, 0x0D,
                0x0C, 0x1A, 0x14, 0x1E},
    14, 0},
    {0xf4, (uint8_t[]){0x00, 0x00, 0xFF}, 3, 0},
    {0xba, (uint8_t[]){0xFF, 0xFF}, 2, 0},
};
#endif
 
#define TAG "CompactWifiBoardLCD"

class CompactWifiBoardLCD : public WifiBoard {
private:
 
    Button boot_button_;
    Display* display_;
    bool use_nimbo_ = true;  // 运行时表情方案（NVS 决定）
    mijin::MotorController motor_;
    mijin::BleAudioPlayer* ble_audio_ = nullptr;

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
#if defined(LCD_TYPE_ILI9341_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
#elif defined(LCD_TYPE_GC9A01_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = gc9107_lcd_init_cmds,
            .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
        };        
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif
        
        esp_lcd_panel_reset(panel);

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
#ifdef  LCD_TYPE_GC9A01_SERIAL
        panel_config.vendor_config = &gc9107_vendor_config;
#endif
        // 运行时读 NVS 决定表情方案（App 可通过 BLE 0x20 切换，无需重烧）
        // 默认值：编译期 CONFIG_BOARD_ENABLE_NIMBO_EMOTION
        bool use_nimbo = true;
        nvs_handle_t nvs;
        if (nvs_open("mijin", NVS_READONLY, &nvs) == ESP_OK) {
            char mode[16] = {0};
            size_t len = sizeof(mode);
            if (nvs_get_str(nvs, "emotion_mode", mode, &len) == ESP_OK) {
                use_nimbo = (strcmp(mode, "default") != 0);
            } else {
                use_nimbo = true;  // 未设置时默认云宝
            }
            nvs_close(nvs);
        }
        use_nimbo_ = use_nimbo;
        ESP_LOGI(TAG, "emotion_mode = %s", use_nimbo_ ? "nimbo" : "default");
        if (use_nimbo_) {
            display_ = new NimboDisplay(panel_io, panel,
                                        DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        } else {
            display_ = new SpiLcdDisplay(panel_io, panel,
                                         DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        }
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
    }

    /* ==================== mijin 离线通道（BLE） ==================== */

    void OnBleCommand(const uint8_t* data, size_t len) {
        if (len < 1) {
            return;
        }
        uint8_t cmd = data[0];
        int arg = (len >= 2) ? data[1] : 0;

        /* 移动指令 0x01~0x09 */
        if (cmd >= 0x01 && cmd <= 0x09) {
            motor_.Execute(cmd, arg);
            return;
        }

        auto* display = GetDisplay();
        switch (cmd) {
        case 0x10: {  // 表情 +1B 表情ID
            static const char* kEmotionNimbo[] = {"idle",    "happy", "thinking", "listening",
                                                   "speaking", "sleeping", "angry",  "surprised"};
            static const char* kEmotionDefault[] = {"neutral",  "happy", "thinking", "listening",
                                                   "speaking", "sleepy", "angry",   "surprised"};
            const char** kEmotionNames = use_nimbo_ ? kEmotionNimbo : kEmotionDefault;
            if (display && arg >= 0 && arg < 8) {
                display->SetEmotion(kEmotionNames[arg]);
            }
            break;
        }
        case 0x11:  // 音频会话开始
            if (ble_audio_) {
                ble_audio_->StartSession();
            }
            break;
        case 0x12:  // 音频会话结束
            if (ble_audio_) {
                ble_audio_->StopSession();
            }
            break;
        case 0x13:  // 查询状态
            NotifyBleStatus();
            break;
        case 0x14:  // 设置速度 0~100
            motor_.SetSpeedLevel(arg);
            break;
        case 0x15:  // 查询版本（握手通道也可读）
            break;
        case 0x20: {  // 切换表情方案：arg=0 官方默认 / arg=1 云宝
            const char* mode = (arg == 0) ? "default" : "nimbo";
            nvs_handle_t nvs;
            if (nvs_open("mijin", NVS_READWRITE, &nvs) == ESP_OK) {
                nvs_set_str(nvs, "emotion_mode", mode);
                nvs_commit(nvs);
                nvs_close(nvs);
                ESP_LOGI(TAG, "emotion_mode switched to %s, rebooting...", mode);
                vTaskDelay(pdMS_TO_TICKS(500));  // 让 BLE notify 发出去
                esp_restart();
            }
            break;
        }
        default:
            ESP_LOGW(TAG, "unknown ble cmd 0x%02x", cmd);
        }
    }

    void NotifyBleStatus() {
        int level = -1;
        bool charging = false;
        bool discharging = false;
        GetBatteryLevel(level, charging, discharging);
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"t\":\"status\",\"batt\":%d,\"ble\":true}", level);
        mijin::MijinBle::GetInstance().Notify(buf);
    }

    void OnBleConnState(bool connected) {
        ESP_LOGI(TAG, "BLE %s", connected ? "connected" : "disconnected");
        if (connected) {
            NotifyBleStatus();
        } else {
            // 断开时清理音频会话：关闭功放输出，防止会话悬挂/持续耗电
            if (ble_audio_) {
                ble_audio_->StopSession();
            }
        }
    }

    void InitializeBle() {
        /* 音频播放器复用 board 的 codec（I2S 功放） */
        ble_audio_ = new mijin::BleAudioPlayer(GetAudioCodec());

        mijin::BleCallbacks cbs;
        cbs.on_command = [this](const uint8_t* d, size_t n) { OnBleCommand(d, n); };
        cbs.on_audio = [this](const uint8_t* d, size_t n) {
            if (ble_audio_) {
                ble_audio_->WritePcm(d, n);
            }
        };
        cbs.on_conn_state = [this](bool c) { OnBleConnState(c); };
        mijin::MijinBle::GetInstance().Init(cbs);
    }

public:
    CompactWifiBoardLCD() :
        boot_button_(BOOT_BUTTON_GPIO) {
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        InitializeTools();
        InitializeBle();
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
        
    }

    ~CompactWifiBoardLCD() {
        delete ble_audio_;
        ble_audio_ = nullptr;
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }
};

DECLARE_BOARD(CompactWifiBoardLCD);
