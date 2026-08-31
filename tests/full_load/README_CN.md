# 满载测试

[English](README.md) | [简体中文](README_CN.md)

## 目的

本固件用于测量规定高负载工作状态下的整板输入电流。测试期间，显示、音频和电机负载持续运行，Wi-Fi 子系统保持初始化但不连接或扫描。

## 启用模块

- M5PM1 和 M5IOE1 电源控制
- AMOLED 显示屏和 M5GFX
- 触摸控制器
- ESP32-S3 Wi-Fi STA 初始化
- ES8311 音频编解码器和 I2S TX / RX
- 电机 PWM
- 共享板级、电源与测试支持

## 模块配置

| 模块 | 配置 |
|---|---|
| M5PM1 | I2C `400 kHz`；启用充电和 LDO；启用 LDO 保持；关闭状态 LED 和外部 5 V 输出 |
| M5IOE1 | I2C `400 kHz`，关闭空闲睡眠；启用 L3B / 显示和音频电源轨 |
| AMOLED | 50% 亮度 (`128/255`)；466 条有效线；自动帧缓冲；不限制帧率，持续随机绘制 |
| 触摸 | CST820 地址 `0x15`，100 kHz；复位拉低 `10 ms`，就绪延迟 `50 ms`；GPIO13 输入 |
| Wi-Fi | 以 `WIFI_PS_MIN_MODEM` 初始化 STA；不连接且不扫描 |
| 音频 | ES8311 地址 `0x18`，I2C `400 kHz`；44.1 kHz、16-bit 立体声、I2S TX / RX、编解码器输入 / 输出模式、30 dB 输入增益、100% 输出音量、RMS 幅度约为满量程 54% 的测试音 |
| 电机 | PWM 通道 0，`5 kHz`，`100%` 占空比 |
| RTOS | Tick 频率 `1000 Hz`；显示任务栈 `4096` 字节；音频任务栈 `6144` 字节 |
| 停止条件 | GPIO1 右键正常停止；最长运行时间 `600 s`；工作任务停止超时 `5 s` |
| 安全门槛 | 运行前必须设置 `CONFIG_M5SW_FULL_LOAD_SAFETY_ACK=y` |

## 运行模式

L1 电源域和触摸控制器保持启用。显示屏亮度设为 128/255，使用 466 条有效线和自动帧缓冲，不限制帧率并持续随机绘制。Wi-Fi 使用 `WIFI_PS_MIN_MODEM` 初始化，但不连接且不扫描。音频以 44.1 kHz、16-bit 立体声 TX / RX、编解码器输入 / 输出模式、30 dB 输入增益和 100% 输出音量运行。电机以 5 kHz 频率和 100% 占空比运行。PM1、IOE1、显示和音频控制路径使用 400 kHz I2C，触摸保持 100 kHz。

## 安全门槛

`CONFIG_M5SW_FULL_LOAD_SAFETY_ACK` 默认关闭。在夹具额定值、供电电流限制、外部温度限制和手动停止流程完成审核并获得批准前，请勿烧录或运行本固件。固件还设置了 600 秒最长运行时间和负载安全关闭机制。

## 测量电流

测量结果中的电流为测量记录中的 `WINDOW Avg`。

- 3.7 V：285.77 mA
- 4.2 V：276.32 mA

[查看测量曲线](../../docs/power-graphs/README_CN.md)。

## 配置

ESP32-S3、16 MB Flash、80 MHz OPI PSRAM、240 MHz CPU、`test_id=full-load`、`config_id=full-load`。

## 构建与烧录

使用 ESP-IDF 5.5.5：

```bash
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

只有在安全评审记录获得批准后才能启用安全确认。确认 `TEST_START`、`BOARD_OK ... DISPLAY_PERCENT=50 DISPLAY_RAW=128 WIFI=initialized WIFI_PS=min_modem WIFI_SCAN=off`、`TEST_READY` 以及最终的 `TEST_PASS` 或 `TEST_FAIL`。进行功耗测量前断开 USB Type-C。
