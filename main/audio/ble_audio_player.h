#pragma once

#include <cstddef>
#include <cstdint>

#include "audio_codec.h"

namespace mijin {

/**
 * @brief BLE 离线通道音频播放器
 *
 * 手机端 TTS 输出 16kHz/16bit/单声道 PCM，经 BLE 音频通道传来；
 * 本模块负责：
 *   1) 16k -> 24k 重采样（2:3 线性插值，匹配 NoAudioCodecSimplex 输出）
 *   2) 环形缓冲（FreeRTOS StreamBuffer）
 *   3) 独立播放任务，持续把数据写入 AudioCodec 输出（喇叭）
 */
class BleAudioPlayer {
public:
    explicit BleAudioPlayer(AudioCodec* codec);
    ~BleAudioPlayer();

    /** 开始 BLE 音频会话（启用输出、清空缓冲） */
    void StartSession();

    /** 结束 BLE 音频会话（停止播放，缓冲丢弃） */
    void StopSession();

    /** 写入一段 16kHz/16bit/单声道 PCM（来自 BLE 音频通道） */
    void WritePcm(const uint8_t* data, size_t len);

    bool SessionActive() const { return session_active_; }

private:
    static void PlayTask(void* param);
    void PlayLoop();

    AudioCodec* codec_ = nullptr;
    void* stream_buffer_ = nullptr;   // StreamBufferHandle_t
    volatile bool session_active_ = false;
    volatile bool exit_ = false;
    void* play_task_ = nullptr;       // TaskHandle_t

    /* 重采样跨包残留：上一个包的最后一个输入样本 */
    int16_t resample_prev_ = 0;
    bool have_prev_ = false;
};

}  // namespace mijin
