#pragma once

#include <cstdint>

namespace mijin {

/**
 * @brief 电机控制器（TB6612 双路直流电机）
 *
 * 指令集（9 个）：前进/后退/左转/右转/停止/加速/减速/自动巡航/跟随(预留)
 * 硬件：TB6612FNG，左轮 AIN1/AIN2/PWMA，右轮 BIN1/BIN2/PWMB
 * 引脚定义见 board config.h 的 MOTOR_*（硬件到手后按实际接线调整）
 */
class MotorController {
public:
    MotorController();
    ~MotorController();

    /** 执行指令；arg 为可选参数（如表情ID/速度值）。返回 false 表示暂不支持 */
    bool Execute(uint8_t cmd, int arg);

    /** 设置左右轮速度：-100(全速后退) ~ +100(全速前进) */
    void SetSpeed(int left, int right);

    /** 停止 */
    void Stop();

    /** 设置速度档位 0~100（后续移动指令使用） */
    void SetSpeedLevel(int level);

    /** 当前速度 0~100 */
    int Speed() const { return speed_; }

    /** 是否处于自动巡航 */
    bool Cruising() const { return cruising_; }

private:
    static void CruiseTask(void* param);
    void CruiseLoop();
    /** 分步等待：每 100ms 检查巡航/退出标志；返回是否自然完成（未被打断） */
    bool WaitCruise(uint32_t ms);

    void SetMotorA(int speed);   // -100~100
    void SetMotorB(int speed);

    uint8_t speed_ = 60;         // 当前巡航速度
    bool cruising_ = false;
    bool exit_ = false;
    void* cruise_task_ = nullptr;
};

}  // namespace mijin
