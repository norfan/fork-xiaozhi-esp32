#pragma once

#include "display/lcd_display.h"
#include "nimbo.h"

/**
 * @brief 云宝表情显示（mijin robot）
 *
 * 基于 SpiLcdDisplay（ST7789/GC9A01 彩屏 + LVGL），屏幕内容：
 *   顶部：时间 + 电量
 *   中部：云宝（Nimbo）8 状态表情，由 xiaozhi 状态机/协议消息驱动
 */
class NimboDisplay : public SpiLcdDisplay {
public:
    NimboDisplay(esp_lcd_panel_io_handle_t io_handle, esp_lcd_panel_handle_t panel_handle,
                 int width, int height, int offset_x, int offset_y, bool mirror_x,
                 bool mirror_y, bool swap_xy);
    ~NimboDisplay() override;

    void SetupUI() override;
    void SetEmotion(const char* emotion) override;
    void SetStatus(const char* status) override;
    void UpdateStatusBar(bool update_all = false) override;
    void SetChatMessage(const char* role, const char* content) override;
    void SetPowerSaveMode(bool on) override;

private:
    static void StatusTimerCb(lv_timer_t* timer);
    void RefreshStatusBar(bool force);

    nimbo_t* nimbo_ = nullptr;
    lv_obj_t* clock_label_ = nullptr;
    lv_obj_t* battery_label_ = nullptr;
    lv_timer_t* status_timer_ = nullptr;
    int last_clock_min_ = -1;
    int last_battery_level_ = -1;
    bool ui_ready_ = false;
    bool power_save_ = false;
};
