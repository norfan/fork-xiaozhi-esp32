#pragma once
/**
 * @file motor_logic.h
 * @brief 电机指令 → 动作的纯逻辑层（无任何 ESP/硬件依赖，可在 PC 上单元测试）。
 *
 * 与 motor_controller.cc 的 Execute() 配合：
 *   Execute() 负责把 MotorAction 落到 GPIO/LEDC，以及巡航任务调度；
 *   本文件只做"指令 → 目标轮速/停止/巡航"的决策，保证决策逻辑可测试。
 *
 * 协议（与 MotionCommands 0x01~0x09 一致）：
 *   0x01 前进 / 0x02 后退 / 0x03 左转(原地) / 0x04 右转(原地) / 0x05 停止
 *   0x06 加速(+10, 上限100) / 0x07 减速(-10, 下限10) / 0x08 自动巡航(启停) / 0x09 跟随(未实现)
 */
#include <cstdint>

namespace mijin {

/** 指令解析结果：需要硬件层执行的动作 */
struct MotorAction {
    bool supported    = false;  ///< 指令是否支持（false → 硬件层打日志返回 false）
    bool stop         = false;  ///< 立即停止（含退出巡航）
    bool cruise_toggle = false; ///< 切换自动巡航（启动/已巡航则忽略）
    bool apply        = false;  ///< 是否把 left/right 写入电机（0x06/0x07 仅改速度档位，不动电机）
    int  left         = 0;      ///< 目标左轮速度 -100~100
    int  right        = 0;      ///< 目标右轮速度 -100~100
};

/** 速度档位上下限（与固件一致）；constexpr 自带内部链接，兼容 C++11+ */
constexpr uint8_t kMotorSpeedMax = 100;
constexpr uint8_t kMotorSpeedMin = 10;

/**
 * 解析电机指令 → 动作。
 * @param cmd  指令码 0x01~0x09
 * @param arg  参数（当前仅 0x14 用，电机指令不用）
 * @param speed in/out 速度档位；0x06/0x07 会更新
 */
inline MotorAction ResolveMotorAction(uint8_t cmd, int arg, uint8_t& speed) {
    MotorAction a;
    switch (cmd) {
    case 0x01:  // 前进
        a.supported = true; a.apply = true; a.left = speed; a.right = speed;
        break;
    case 0x02:  // 后退
        a.supported = true; a.apply = true; a.left = -(int)speed; a.right = -(int)speed;
        break;
    case 0x03:  // 左转（原地）
        a.supported = true; a.apply = true; a.left = -(int)speed; a.right = speed;
        break;
    case 0x04:  // 右转（原地）
        a.supported = true; a.apply = true; a.left = speed; a.right = -(int)speed;
        break;
    case 0x05:  // 停止
        a.supported = true; a.stop = true;
        break;
    case 0x06:  // 加速（上限 100）
        a.supported = true;
        speed = (uint8_t)(speed + 10 > kMotorSpeedMax ? kMotorSpeedMax : speed + 10);
        break;
    case 0x07:  // 减速（下限 10）
        a.supported = true;
        speed = (uint8_t)(speed - 10 < kMotorSpeedMin ? kMotorSpeedMin : speed - 10);
        break;
    case 0x08:  // 自动巡航（启停）
        a.supported = true; a.cruise_toggle = true;
        break;
    case 0x09:  // 跟随：预留，未实现
        break;
    default:
        break;
    }
    (void)arg;
    return a;
}

}  // namespace mijin
