# mijin 智能小车机器人（ESP32-S3 固件）

> 基于 [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) fork 的二次开发版本，目标是做一台**带表情屏、语音对话、手机蓝牙/WiFi 遥控**的四轮智能小车。

## 项目架构

```
┌─────────────────────────────────────────────────┐
│  Android App（mijin-robot）                     │
│  ├─ 端侧 LLM（Qwen2.5-1.5B）+ ASR + VITS TTS  │
│  ├─ 故事大王流式讲故事模式                       │
│  └─ RobotTransport：BLE / WiFi/TCP 双通道       │
└──────────────┬──────────────────┬──────────────┘
               │ BLE Audio         │ WiFi/TCP（PC 模拟器调试用）
               ▼                  ▼
┌─────────────────────────────────────────────────┐
│  ESP32-S3（本仓库）                             │
│  ├─ bread-compact-wifi-lcd 板级支持             │
│  ├─ nimbo_display：240×240 LCD 云宝表情屏       │
│  ├─ motor_controller：TB6612 四轮差速控制        │
│  ├─ ble_audio_player：BLE 音频接收与播放         │
│  └─ xiaozhi-esp32 原生联网对话能力              │
└─────────────────────────────────────────────────┘
```

## 定制内容（相对于上游 xiaozhi-esp32）

### 板级：bread-compact-wifi-lcd

位于 `main/boards/bread-compact-wifi-lcd/`：

- **`config.h`**：引脚定义（屏幕、电机、BLE、电池 ADC）
- **`nimbo_display.cc / .h`**：240×240 SPI LCD 驱动，移植 aora-bot nimbo 云宝表情
- **`nimbo.c / .h`**：nimbo 表情数据（8 状态：idle / listening / thinking / speaking / action / happy / sleeping / resetIdle）
- **`motor_controller.cc / .h`**：TB6612FNG 驱动，四轮差速
- **`motor_logic.h`**：运动指令集（前进/后退/左转/右转/停止/速度档位）

### BLE 音频通道

`main/audio/ble_audio_player.cc / .h`：接收 Android App 通过 BLE 推送的 16kHz PCM 音频流，直接送 I2S DAC 播放。用于：
- 离线模式：手机端 VITS TTS 生成语音后通过 BLE 传给小车播放
- 联网模式：仍走 xiaozhi-esp32 原生 WebSocket/MQTT 通道

### 表情协议

Android App 通过 BLE/WiFi 发送表情指令（0x10 + 0~7），小车屏幕切换对应云宝表情：

| 指令 | 表情 | 说明 |
|---|---|---|
| 0 | idle | 待机 |
| 1 | listening | 聆听中 |
| 2 | thinking | 思考中 |
| 3 | speaking | 说话中（嘴巴开合） |
| 4 | action | 动作中 |
| 5 | happy | 开心 |
| 6 | sleeping | 睡觉 |
| 7 | resetIdle | 重置 idle 计时器 |

## 硬件清单（目标）

- ESP32-S3-WROOM-1（N16R8）
- 240×240 SPI LCD（ST7789）
- TB6612FNG 电机驱动
- 4× N20 减速电机 + 轮子
- MAX98357A I2S 功放 + 喇叭
- INMP441 麦克风
- 3.7V 锂电池 + TP4056 Type-C 充电模块
- MT3608 升压到 5V
- 2× 面包板

## 开发环境

- ESP-IDF v6.0.1+（推荐 v6.1）
- VS Code + ESP-IDF 插件
- 构建：`idf.py set-target esp32s3 && idf.py build flash monitor`

## 相关仓库

- **Android App**：[norfan/mijin-robot](https://github.com/norfan/mijin-robot)
- **PC TCP 模拟器**：本仓库 `ble-simulator/` 目录下，无硬件时在 PC 上模拟小车屏幕和运动指令
- **表情引擎来源**：[aora-bot](https://github.com/78/aora-bot)（mood-mates nimbo）

---

## 上游 xiaozhi-esp32 说明

本仓库 fork 自 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)，保留其全部联网对话、WebSocket/MQTT、Opus 音频流、ESP-SR 离线唤醒等能力。上游 README 见 [README_upstream.md](README_upstream.md)。