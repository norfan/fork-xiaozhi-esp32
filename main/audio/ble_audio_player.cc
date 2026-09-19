#include "ble_audio_player.h"

#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>
#include <freertos/task.h>

#include <vector>

#define TAG "BleAudioPlayer"

/* 输出采样率（与 board config AUDIO_OUTPUT_SAMPLE_RATE 一致） */
#define BLE_OUT_SAMPLE_RATE 24000
/* 输入采样率（手机 TTS 输出约定） */
#define BLE_IN_SAMPLE_RATE  16000

/* 环形缓冲：~1.5 秒 24k 音频（24k*2B*1.5 = 72KB） */
#define BLE_SB_SIZE (24 * 1024 * 3)
/* 播放任务每次写 480 样本 = 20ms @24k */
#define BLE_PLAY_CHUNK_SAMPLES 480
#define BLE_PLAY_TASK_STACK    (4096)
#define BLE_PLAY_TASK_PRIO     (6)

namespace mijin {

BleAudioPlayer::BleAudioPlayer(AudioCodec* codec) : codec_(codec) {
    stream_buffer_ = xStreamBufferCreate(BLE_SB_SIZE, 1);
    if (!stream_buffer_) {
        ESP_LOGE(TAG, "stream buffer create failed");
        return;
    }
    xTaskCreate(PlayTask, "mijin_ble_audio", BLE_PLAY_TASK_STACK, this, BLE_PLAY_TASK_PRIO,
                (TaskHandle_t*)&play_task_);
    ESP_LOGI(TAG, "player created, buffer=%d bytes", BLE_SB_SIZE);
}

BleAudioPlayer::~BleAudioPlayer() {
    exit_ = true;
    session_active_ = false;
    if (play_task_) {
        vTaskDelete((TaskHandle_t)play_task_);
        play_task_ = nullptr;
    }
    if (stream_buffer_) {
        vStreamBufferDelete((StreamBufferHandle_t)stream_buffer_);
        stream_buffer_ = nullptr;
    }
}

void BleAudioPlayer::StartSession() {
    ESP_LOGI(TAG, "audio session start");
    if (stream_buffer_) {
        xStreamBufferReset((StreamBufferHandle_t)stream_buffer_);
    }
    have_prev_ = false;
    session_active_ = true;
    if (codec_) {
        codec_->EnableOutput(true);
    }
}

void BleAudioPlayer::StopSession() {
    ESP_LOGI(TAG, "audio session stop");
    session_active_ = false;
    if (stream_buffer_) {
        xStreamBufferReset((StreamBufferHandle_t)stream_buffer_);
    }
    have_prev_ = false;
    // 与 StartSession 的 EnableOutput(true) 对称：关掉功放输出，避免静音耗电
    if (codec_) {
        codec_->EnableOutput(false);
    }
}

void BleAudioPlayer::WritePcm(const uint8_t* data, size_t len) {
    if (!session_active_ || !stream_buffer_) {
        return;
    }
    if (len == 0 || (len & 1)) {
        ESP_LOGW(TAG, "invalid pcm len %u", (unsigned)len);
        return;
    }

    size_t in_samples = len / 2;
    const int16_t* in = (const int16_t*)data;

    /* 输出容量：in*1.5 + 1，双缓冲暂存 */
    size_t out_cap = in_samples * 3 / 2 + 2;
    int16_t* out = (int16_t*)malloc(out_cap * sizeof(int16_t));
    if (!out) {
        ESP_LOGW(TAG, "alloc failed, dropping %u bytes", (unsigned)len);
        return;
    }

    size_t n_out = 0;
    if (have_prev_) {
        /* 补插值：上一个样本与本次第一个样本之间 */
        out[n_out++] = (int16_t)(((int32_t)resample_prev_ + in[0]) / 2);
        have_prev_ = false;
    }

    /* 2:3 线性插值：in[2k],in[2k+1] -> out[3k]=in[2k], out[3k+1]=(in[2k]+in[2k+1])/2, out[3k+2]=in[2k+1] */
    size_t k = 0;
    for (; k + 1 < in_samples; k += 2) {
        out[n_out++] = in[k];
        out[n_out++] = (int16_t)(((int32_t)in[k] + in[k + 1]) / 2);
        out[n_out++] = in[k + 1];
    }
    if (k < in_samples) {
        /* 剩余 1 个样本：留作下一包插值 */
        resample_prev_ = in[k];
        have_prev_ = true;
    }

    size_t bytes = n_out * sizeof(int16_t);
    size_t sent = xStreamBufferSend((StreamBufferHandle_t)stream_buffer_, out, bytes,
                                    pdMS_TO_TICKS(50));
    if (sent < bytes) {
        ESP_LOGW(TAG, "buffer full, dropped %u/%u bytes", (unsigned)(bytes - sent),
                 (unsigned)bytes);
    }
    free(out);
}

void BleAudioPlayer::PlayTask(void* param) {
    static_cast<BleAudioPlayer*>(param)->PlayLoop();
}

void BleAudioPlayer::PlayLoop() {
    int16_t chunk[BLE_PLAY_CHUNK_SAMPLES];
    while (!exit_) {
        if (!session_active_ || !stream_buffer_ || !codec_) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        size_t got = xStreamBufferReceive((StreamBufferHandle_t)stream_buffer_, chunk,
                                          sizeof(chunk), pdMS_TO_TICKS(100));
        if (got > 0) {
            size_t samples = got / sizeof(int16_t);
            std::vector<int16_t> out(chunk, chunk + samples);
            codec_->OutputData(out);
        }
    }
    ESP_LOGI(TAG, "play task exit");
}

}  // namespace mijin
