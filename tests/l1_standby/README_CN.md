# L1 待机测试

[English](README.md) | [简体中文](README_CN.md)

## 目的

本固件用于测量 M5StopWatch 处于 L1 待机状态、RTC 和 IMU 保持供电时的整板输入电流。

## 启用模块

- M5PM1 电源管理器
- RX8130 RTC
- BMI270 IMU
- M5StopWatch 共享板级支持
- 测试日志和状态支持

此基线测试不启用 RTC Alarm、RTC 定时器或 IMU 运动唤醒源。M5IOE1、显示屏、触摸、Wi-Fi、音频和电机负载保持关闭。

## 模块配置

| 模块 | 配置 |
|---|---|
| M5PM1 | I2C 地址 `0x6E`，100 kHz；关闭充电和升压；启用 LDO 保持；使用 PM1 关机命令进入 L1 |
| RX8130 | I2C 地址 `0x32`，100 kHz；关闭 Alarm、定时器和更新中断 |
| BMI270 | I2C 地址 `0x68`，100 kHz；初始化 Base API；关闭任意运动检测和 INT1 |
| 唤醒源 | 无 |
| 准备过程 | `TEST_READY` 后延迟 3 秒再进入 L1 |

## 运行模式

固件初始化 PM1、RX8130 和 BMI270，关闭其唤醒源，报告板级就绪状态，等待 3 秒后进入 PM1 L1 待机。PM1 切断 ESP32-S3 供电并保持 L1 电源轨前，`TEST_READY` 是最后一条预期串口消息。

## 测量电流

测量结果中的电流为测量记录中的 `WINDOW Avg`。

- 3.7 V：20.49 uA
- 4.2 V：20.67 uA

[查看测量曲线](../../docs/power-graphs/README_CN.md)。

## 配置

| 设置 | 值 |
|---|---|
| 目标 | ESP32-S3 |
| Flash | 16 MB |
| PSRAM | OPI，80 MHz |
| CPU | 240 MHz |
| I2C | 100 kHz |
| 测试 ID | `l1-standby` |
| 配置 ID | `l1-standby` |

## 构建与烧录

在此目录中使用 ESP-IDF 5.5.5：

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

串口监视器仅用于功能验证。进行功耗测量前，断开 USB Type-C 并停止监视。生成的构建文件属于本地产物，不构成测试配置的一部分。

## 预期验证

确认 `TEST_START`、`BOARD_OK PM1=1 RTC=1 IMU=1` 和 `TEST_READY`。`TEST_READY` 后不应出现周期诊断日志。
