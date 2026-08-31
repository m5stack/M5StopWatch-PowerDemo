# L2 深度睡眠测试

[English](README.md) | [简体中文](README_CN.md)

## 目的

本固件用于测量 M5StopWatch 处于无唤醒源的 L2 深度睡眠状态时的整板输入电流，并验证外设在 ESP32-S3 进入 Deep Sleep 前切换到规定的低功耗状态。

## 启用模块

- M5PM1 电源管理器
- M5IOE1 外设控制器
- ESP32-S3 深度睡眠
- M5StopWatch 共享板级与测试支持

此测试关闭 RTC 和 IMU 唤醒源，同时关闭显示屏、触摸、Wi-Fi、音频和电机负载。

## 模块配置

| 模块 | 配置 |
|---|---|
| M5PM1 | I2C 地址 `0x6E`，100 kHz；启用 DCDC 和 LDO；关闭充电、升压和状态 LED；启用 LDO 保持；看门狗超时 `0`；I2C 空闲睡眠时间 `1 s` |
| M5IOE1 | I2C 地址 `0x4F`，100 kHz；恢复出厂设置，负载控制输出保持安全高阻状态；停止 PWM；空闲睡眠时间 `1 s` |
| ESP32-S3 | 关闭所有唤醒源后进入 Deep Sleep |
| 唤醒源 | 无；不启用 PM1 聚合 IRQ 输出 |
| 准备过程 | 应用延迟 3 秒，随后执行最后 1 秒的 L2 稳定等待 |

## 运行模式

固件初始化 PM1，使用 `WakeSelection::none` 准备 L2，关闭外部负载，配置安全高阻状态和 I2C 空闲睡眠，报告就绪状态，等待 3 秒后进入没有唤醒源的 ESP32-S3 深度睡眠。

## 测量电流

测量结果中的电流为测量记录中的 `WINDOW Avg`。

- 3.7 V：90.06 uA
- 4.2 V：51.65 uA

[查看测量曲线](../../docs/power-graphs/README_CN.md)。

## 配置

| 设置 | 值 |
|---|---|
| 目标 | ESP32-S3 |
| Flash | 16 MB |
| PSRAM | OPI，80 MHz |
| CPU | 240 MHz |
| I2C | 100 kHz |
| 测试 ID | `l2-deepsleep` |
| 配置 ID | `l2-deepsleep` |

## 构建与烧录

使用 ESP-IDF 5.5.5：

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

串口监视仅用于功能验证。进行功耗测量前，断开 USB Type-C 并停止监视。

## 预期验证

确认 `TEST_START`、`BOARD_OK PM1=1 IOE1=1 WAKE=none` 和 `TEST_READY`。固件应进入深度睡眠，且不再发生外设事务。
