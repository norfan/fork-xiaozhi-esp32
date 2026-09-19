/**
 * @file nimbo.h
 * @brief 云宝 (Nimbo) 表情模块 — LVGL 实现的蓬松云朵角色
 *
 * 参考 aora-bot mood-mates 的云宝设计参数，用 LVGL 图形原语重绘。
 * 支持 8 种表情状态 + 呼吸/眨眼/眼神/说话等动画。
 * 目标环境：PC 模拟器 (LVGL 9.6) 与 ESP32-S3 (LVGL 9.5)，API 兼容。
 */
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 表情状态 ID（与需求文档映射表一致） */
#define NIMBO_EMOTION_IDLE       0   /* 待机 */
#define NIMBO_EMOTION_HAPPY      1   /* 开心 */
#define NIMBO_EMOTION_THINKING   2   /* 思考 */
#define NIMBO_EMOTION_LISTENING  3   /* 聆听 */
#define NIMBO_EMOTION_SPEAKING   4   /* 说话 */
#define NIMBO_EMOTION_SLEEPING   5   /* 睡觉 */
#define NIMBO_EMOTION_ANGRY      6   /* 生气 */
#define NIMBO_EMOTION_SURPRISED  7   /* 惊讶 */
#define NIMBO_EMOTION_COUNT      8

typedef struct nimbo_t nimbo_t;

/**
 * @brief 创建云宝（挂到 parent 上，占据 parent 全部区域）
 */
nimbo_t * nimbo_create(lv_obj_t * parent);

/**
 * @brief 切换表情
 */
void nimbo_set_emotion(nimbo_t * nb, int emotion);

/**
 * @brief 获取当前表情
 */
int nimbo_get_emotion(const nimbo_t * nb);

/**
 * @brief 销毁云宝
 */
void nimbo_delete(nimbo_t * nb);

#ifdef __cplusplus
}
#endif
