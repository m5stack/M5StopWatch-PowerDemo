# RTC Alarm 唤醒 L2 测试

[English](README.md) | [简体中文](README_CN.md)

## 目的

本固件用于验证 RX8130 RTC Alarm 通过 PM1 GPIO0 和 `ext0` 路径，从 ESP32-S3 Deep Sleep 中唤醒设备，并测量设备等待唤醒期间的整板输入电流。

## 启用模块

- M5PM1 电源管理器和 GPIO0 唤醒路径
- M5IOE1 L2 低功耗状态配置
- RX8130 RTC Alarm
- 用于排除非目标唤醒事件的 BMI270 状态检查
- ESP32-S3 深度睡眠和 `ext0` 唤醒
- 共享唤醒与测试支持

## 模块配置

| 模块 | 配置 |
|---|---|
| M5PM1 | I2C 地址 `0x6E`，100 kHz；启用 DCDC 和 LDO；看门狗超时 `0`；GPIO0 下降沿唤醒；GPIO1 聚合 IRQ 输出；I2C 空闲睡眠时间 `1 s` |
| M5IOE1 | I2C 地址 `0x4F`，100 kHz；关闭外部负载；停止 PWM；空闲睡眠时间 `1 s` |
| RX8130 | I2C 地址 `0x32`，100 kHz；设置已知 RTC 时间；配置 `120 s` 后触发一次的 Alarm |
| BMI270 | 关闭任意运动检测；检查保留状态以排除非目标唤醒事件 |
| ESP32-S3 唤醒 | GPIO12 上的 `ext0`，低电平有效 |
| 唤醒路径 | RX8130 IRQ -> PM1 GPIO0 -> PM1 GPIO1 -> ESP32 GPIO12 |
| 验收条件 | ESP `ext0`、PM1 GPIO0 和 RTC Alarm 状态必须一致；IMU 任意运动检测标志必须保持未置位 |

## 运行模式

固件将外设切换到规定的 L2 低功耗状态，启用 PM1 GPIO0 和 ESP32 GPIO12 唤醒链路，配置 120 秒后单次触发的 RX8130 Alarm，报告就绪状态，等待规定的稳定时间后进入 Deep Sleep。唤醒后，固件验证 ESP、PM1 和 RTC 状态，清除已记录的状态标志并报告结果。

## 测量电流

测量结果中的电流为测量记录中的 `WINDOW Avg`。

- 3.7 V：57.06 uA
- 4.2 V：43.44 uA

[查看测量曲线](../../docs/power-graphs/README_CN.md)。

## 配置

ESP32-S3、16 MB Flash、80 MHz OPI PSRAM、240 MHz CPU 和 100 kHz I2C。使用 ESP-IDF 5.5.5。

## 构建与烧录

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

预期日志包括 `RTC_CONFIG_OK`、`BOARD_OK`、`WAKE_ARMED`、`TEST_READY`、`WAKE_CAPTURE`、`WAKE_CLEAR_OK` 和 `TEST_PASS`。进行功耗测量前断开 USB Type-C。
