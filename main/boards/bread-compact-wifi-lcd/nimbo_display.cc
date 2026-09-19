#include "nimbo_display.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <lvgl.h>

#include <ctime>
#include <string>

#include "board.h"

#define TAG "NimboDisplay"

LV_FONT_DECLARE(font_noto_sans_basic_16_4);

/* ==================== 状态 -> 云宝表情映射 ==================== */
static int MapEmotion(const char* emotion) {
    if (!emotion || emotion[0] == '\0') {
        return NIMBO_EMOTION_IDLE;
    }
    const std::string s(emotion);
    if (s == "listening") return NIMBO_EMOTION_LISTENING;
    if (s == "speaking") return NIMBO_EMOTION_SPEAKING;
    if (s == "thinking" || s == "connecting") return NIMBO_EMOTION_THINKING;
    if (s == "sleepy" || s == "sleeping" || s == "dormant") return NIMBO_EMOTION_SLEEPING;
    if (s == "warning" || s == "angry") return NIMBO_EMOTION_ANGRY;
    if (s == "surprised" || s == "amazed" || s == "shocked") return NIMBO_EMOTION_SURPRISED;
    if (s == "neutral" || s == "happy" || s == "smile") return NIMBO_EMOTION_HAPPY;
    return NIMBO_EMOTION_IDLE;
}

NimboDisplay::NimboDisplay(esp_lcd_panel_io_handle_t io_handle,
                           esp_lcd_panel_handle_t panel_handle, int width, int height,
                           int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                           bool swap_xy)
    : SpiLcdDisplay(io_handle, panel_handle, width, height, offset_x, offset_y, mirror_x,
                    mirror_y, swap_xy) {}

NimboDisplay::~NimboDisplay() {
    if (status_timer_) {
        lv_timer_delete(status_timer_);
        status_timer_ = nullptr;
    }
    if (nimbo_) {
        nimbo_delete(nimbo_);
        nimbo_ = nullptr;
    }
}

void NimboDisplay::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }
    Display::SetupUI();
    DisplayLockGuard lock(this);

    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x10151F), 0);

    /* 顶部状态栏：时间（居中）+ 电量（右上） */
    clock_label_ = lv_label_create(screen);
    lv_obj_set_style_text_color(clock_label_, lv_color_hex(0x9FB3D6), 0);
    lv_obj_set_style_text_font(clock_label_, &font_noto_sans_basic_16_4, 0);
    lv_obj_set_pos(clock_label_, 0, 6);
    lv_obj_set_width(clock_label_, width_);
    lv_obj_set_style_text_align(clock_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(clock_label_, "--:--");

    battery_label_ = lv_label_create(screen);
    lv_obj_set_style_text_color(battery_label_, lv_color_hex(0x9FB3D6), 0);
    lv_obj_set_style_text_font(battery_label_, &font_noto_sans_basic_16_4, 0);
    lv_obj_align(battery_label_, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_label_set_text(battery_label_, "");

    /* 中部：云宝 */
    nimbo_ = nimbo_create(screen);
    nimbo_set_emotion(nimbo_, NIMBO_EMOTION_IDLE);

    /* 周期刷新时间/电量（LVGL 定时器由 lvgl 任务驱动） */
    status_timer_ = lv_timer_create(StatusTimerCb, 5000, this);

    ui_ready_ = true;
    RefreshStatusBar(true);
    ESP_LOGI(TAG, "NimboDisplay UI ready");
}

void NimboDisplay::SetEmotion(const char* emotion) {
    ESP_LOGI(TAG, "SetEmotion: %s", emotion ? emotion : "(null)");
    if (!nimbo_) {
        return;
    }
    DisplayLockGuard lock(this);
    nimbo_set_emotion(nimbo_, MapEmotion(emotion));
}

void NimboDisplay::SetStatus(const char* status) {
    ESP_LOGI(TAG, "SetStatus: %s", status ? status : "(null)");
    (void)status;
}

void NimboDisplay::StatusTimerCb(lv_timer_t* timer) {
    auto* self = static_cast<NimboDisplay*>(lv_timer_get_user_data(timer));
    if (self) {
        self->RefreshStatusBar(false);
    }
}

void NimboDisplay::RefreshStatusBar(bool force) {
    if (!ui_ready_ || !clock_label_ || !battery_label_) {
        return;
    }

    /* 时间 */
    char buf[16];
    time_t now = time(nullptr);
    struct tm tmv {};
    localtime_r(&now, &tmv);
    strftime(buf, sizeof(buf), "%H:%M", &tmv);
    int cur_min = tmv.tm_hour * 60 + tmv.tm_min;
    if (force || cur_min != last_clock_min_) {
        last_clock_min_ = cur_min;
        DisplayLockGuard lock(this);
        lv_label_set_text(clock_label_, buf);
    }

    /* 电量（无电池检测硬件时显示占位） */
    int level = -1;
    bool charging = false;
    bool discharging = false;
    bool ok = Board::GetInstance().GetBatteryLevel(level, charging, discharging);
    if (force || level != last_battery_level_) {
        last_battery_level_ = level;
        char bat[32];
        if (ok && level >= 0) {
            snprintf(bat, sizeof(bat), "电池%d%%", level);
        } else {
            snprintf(bat, sizeof(bat), "电池--%%");
        }
        DisplayLockGuard lock(this);
        lv_label_set_text(battery_label_, bat);
    }
}

void NimboDisplay::UpdateStatusBar(bool update_all) {
    RefreshStatusBar(update_all);
}

void NimboDisplay::SetChatMessage(const char* role, const char* content) {
    ESP_LOGI(TAG, "ChatMessage(%s): %s", role ? role : "", content ? content : "");
}

void NimboDisplay::SetPowerSaveMode(bool on) {
    ESP_LOGI(TAG, "SetPowerSaveMode: %d", on);
    power_save_ = on;
    DisplayLockGuard lock(this);
    if (clock_label_) {
        lv_obj_add_flag(clock_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (battery_label_) {
        lv_obj_add_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (nimbo_) {
        nimbo_set_emotion(nimbo_, on ? NIMBO_EMOTION_SLEEPING : NIMBO_EMOTION_IDLE);
    }
}
