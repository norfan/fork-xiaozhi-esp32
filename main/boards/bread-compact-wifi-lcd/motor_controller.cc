#include "motor_controller.h"
#include "motor_logic.h"

#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/ledc.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "config.h"

#define TAG "MotorController"

/* LEDC：两路 PWM（1kHz，8bit），分别驱动 PWMA / PWMB */
#define MOTOR_PWM_FREQ_HZ  1000
#define MOTOR_PWM_RES      LEDC_TIMER_8_BIT
#define MOTOR_TIMER_A      LEDC_TIMER_0
#define MOTOR_TIMER_B      LEDC_TIMER_1
#define MOTOR_CH_A         LEDC_CHANNEL_0
#define MOTOR_CH_B         LEDC_CHANNEL_1

#define MOTOR_CRUISE_TASK_STACK (3072)
#define MOTOR_CRUISE_TASK_PRIO  (4)

/* 巡航超时保护：连续巡航超过该时长自动停止（防忘记停/空跑耗电/失控） */
#define MOTOR_CRUISE_MAX_MS     (60000)
/* 巡航循环步进：停止/断开响应 ≤ 该值（ms） */
#define MOTOR_CRUISE_STEP_MS    (100)

namespace mijin {

/* ==================== 引脚初始化 ==================== */

static void MotorPinInit() {
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << MOTOR_AIN1) | (1ULL << MOTOR_AIN2) | (1ULL << MOTOR_BIN1) |
                      (1ULL << MOTOR_BIN2);
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io);
    gpio_set_level(MOTOR_AIN1, 0);
    gpio_set_level(MOTOR_AIN2, 0);
    gpio_set_level(MOTOR_BIN1, 0);
    gpio_set_level(MOTOR_BIN2, 0);
}

static void MotorPwmInit() {
    ledc_timer_config_t timer_a = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = MOTOR_PWM_RES,
        .timer_num = MOTOR_TIMER_A,
        .freq_hz = MOTOR_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_a);

    ledc_timer_config_t timer_b = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = MOTOR_PWM_RES,
        .timer_num = MOTOR_TIMER_B,
        .freq_hz = MOTOR_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_b);

    ledc_channel_config_t ch_a = {
        .gpio_num = MOTOR_PWMA,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = MOTOR_CH_A,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = MOTOR_TIMER_A,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&ch_a);

    ledc_channel_config_t ch_b = {
        .gpio_num = MOTOR_PWMB,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = MOTOR_CH_B,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = MOTOR_TIMER_B,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&ch_b);
}

/* ==================== 构造/析构 ==================== */

MotorController::MotorController() {
    MotorPinInit();
    MotorPwmInit();
    Stop();
    ESP_LOGI(TAG, "motor controller ready (AIN1=%d AIN2=%d PWMA=%d / BIN1=%d BIN2=%d PWMB=%d)",
             MOTOR_AIN1, MOTOR_AIN2, MOTOR_PWMA, MOTOR_BIN1, MOTOR_BIN2, MOTOR_PWMB);
}

MotorController::~MotorController() {
    exit_ = true;
    cruising_ = false;
    Stop();
    if (cruise_task_) {
        vTaskDelete((TaskHandle_t)cruise_task_);
        cruise_task_ = nullptr;
    }
}

/* ==================== 底层驱动 ==================== */

void MotorController::SetMotorA(int speed) {
    /* speed: -100 ~ +100；负=反转 */
    gpio_set_level(MOTOR_AIN1, speed >= 0 ? 1 : 0);
    gpio_set_level(MOTOR_AIN2, speed >= 0 ? 0 : 1);
    uint32_t duty = (uint32_t)((speed < 0 ? -speed : speed) * 255 / 100);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, MOTOR_CH_A, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, MOTOR_CH_A);
}

void MotorController::SetMotorB(int speed) {
    gpio_set_level(MOTOR_BIN1, speed >= 0 ? 1 : 0);
    gpio_set_level(MOTOR_BIN2, speed >= 0 ? 0 : 1);
    uint32_t duty = (uint32_t)((speed < 0 ? -speed : speed) * 255 / 100);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, MOTOR_CH_B, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, MOTOR_CH_B);
}

void MotorController::SetSpeed(int left, int right) {
    if (left > 100) left = 100;
    if (left < -100) left = -100;
    if (right > 100) right = 100;
    if (right < -100) right = -100;
    SetMotorA(left);
    SetMotorB(right);
}

void MotorController::Stop() {
    SetMotorA(0);
    SetMotorB(0);
}

void MotorController::SetSpeedLevel(int level) {
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    speed_ = (uint8_t)level;
    ESP_LOGI(TAG, "speed level -> %d", speed_);
}

/* ==================== 自动巡航 ==================== */

void MotorController::CruiseTask(void* param) {
    static_cast<MotorController*>(param)->CruiseLoop();
}

void MotorController::CruiseLoop() {
    /* 巡游：前进 3s -> 原地左转 1.2s -> 前进…（后续可加陀螺仪走直线）。
     * 保护：
     *   1) 超时保护：总巡航时长达 MOTOR_CRUISE_MAX_MS 自动停止（防忘记停/失控）；
     *   2) 快速响应：大延时拆成 100ms 步进，cruising_/exit_ 变化 ≤100ms 生效。 */
    uint32_t elapsed = 0;
    while (!exit_ && cruising_ && elapsed < MOTOR_CRUISE_MAX_MS) {
        SetSpeed(speed_, speed_);
        if (!WaitCruise(3000)) break;
        elapsed += 3000;
        if (elapsed >= MOTOR_CRUISE_MAX_MS || !cruising_ || exit_) break;
        SetSpeed(-speed_, speed_);
        if (!WaitCruise(1200)) break;
        elapsed += 1200;
    }
    Stop();
    cruising_ = false;
}

/** 分步等待：每 100ms 检查一次 cruising_/exit_；返回是否自然等待完成 */
bool MotorController::WaitCruise(uint32_t ms) {
    while (ms > 0 && cruising_ && !exit_) {
        uint32_t step = ms > MOTOR_CRUISE_STEP_MS ? MOTOR_CRUISE_STEP_MS : ms;
        vTaskDelay(pdMS_TO_TICKS(step));
        ms -= step;
    }
    return cruising_ && !exit_;
}

/* ==================== 指令执行 ==================== */

bool MotorController::Execute(uint8_t cmd, int arg) {
    MotorAction a = ResolveMotorAction(cmd, arg, speed_);
    if (!a.supported) {
        ESP_LOGW(TAG, "unsupported motor cmd 0x%02x", cmd);
        return false;
    }
    if (a.stop) {
        cruising_ = false;
        Stop();
        return true;
    }
    if (a.cruise_toggle) {
        if (!cruising_) {
            cruising_ = true;
            xTaskCreate(CruiseTask, "mijin_cruise", MOTOR_CRUISE_TASK_STACK, this,
                        MOTOR_CRUISE_TASK_PRIO, (TaskHandle_t*)&cruise_task_);
        }
        return true;
    }
    if (a.apply) {
        /* 任何方向指令都会打断巡航 */
        cruising_ = false;
        SetSpeed(a.left, a.right);
    }
    return true;
}

}  // namespace mijin
