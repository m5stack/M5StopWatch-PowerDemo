# RTC 定时器唤醒 L1 测试

[English](README.md) | [简体中文](README_CN.md)

## 目的

本固件用于验证 M5StopWatch 处于 L1 待机时的 RX8130 倒计时定时器唤醒，并测量设备等待唤醒期间的整板输入电流。

## 启用模块

- M5PM1 电源管理器
- RX8130 倒计时定时器
- 用于排除非目标唤醒事件的 BMI270 IMU
- PM1 GPIO0 唤醒路径
- 共享唤醒与测试支持

## 模块配置

| 模块 | 配置 |
|---|---|
| M5PM1 | I2C 地址 `0x6E`，100 kHz；启用 LDO 保持；GPIO0 为带上拉的输入并启用下降沿唤醒；关闭 GPIO4 唤醒 |
| RX8130 | I2C 地址 `0x32`，100 kHz；一次性倒计时定时器 `120 s`，1 Hz 定时器源 |
| BMI270 | I2C 地址 `0x68`，100 kHz；关闭任意运动检测；仅检查状态以排除非目标唤醒事件 |
| 唤醒路径 | RX8130 IRQ -> PM1 GPIO0 -> PM1 L1 唤醒 |
| 验收条件 | RTC 定时器标志必须与 PM1 GPIO0 状态一致；RTC Alarm 和 IMU 任意运动检测标志必须保持未置位 |

## 运行模式

固件准备 L1，配置 RX8130 一次性 120 秒定时器，关闭无关唤醒源，报告就绪状态并进入 L1 待机。唤醒后，固件验证 RTC 定时器事件，清除已记录的状态标志并报告结果。

## 测量电流

测量结果中的电流为测量记录中的 `WINDOW Avg`。

- 3.7 V：20.46 uA
- 4.2 V：20.52 uA

[查看测量曲线](../../docs/power-graphs/README_CN.md)。

## 配置

ESP32-S3、16 MB Flash、80 MHz OPI PSRAM、240 MHz CPU、100 kHz I2C、`test_id=rtc-time-wake-l1`、`config_id=rtc-time-wake-l1`。

## 构建与烧录

使用 ESP-IDF 5.5.5：

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

确认 `RTC_CONFIG_OK`、`BOARD_OK`、`WAKE_ARMED`、`TEST_READY`、`WAKE_CAPTURE`、`WAKE_CLEAR_OK` 和 `TEST_PASS`。进行功耗测量前断开 USB Type-C。
