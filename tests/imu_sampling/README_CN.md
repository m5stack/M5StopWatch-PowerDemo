# IMU 采样测试

[English](README.md) | [简体中文](README_CN.md)

## 目的

本固件用于测量 BMI270 持续采样、其他可选负载关闭时的整板输入电流。

## 启用模块

- M5PM1 电源管理器
- M5IOE1 板级控制
- BMI270 IMU
- 共享板级与测试支持

## 模块配置

| 模块 | 配置 |
|---|---|
| BMI270 API | Context 模式，通过 AUX 连接 BMM150 磁力计 |
| 加速度计 | +/-4 g，200 Hz |
| 陀螺仪 | +/-1000 dps，200 Hz |
| 启用传感器 | 加速度计、陀螺仪和 AUX |
| 读取 API | `get_sensor_data()` |
| 调用周期 | 10 ms |
| I2C | 地址 `0x68`，100 kHz |

| 支持模块 | 配置 |
|---|---|
| M5PM1 | 启用 L1 电源轨；关闭状态 LED 和外部 5 V 输出 |
| M5IOE1 | 100 kHz；关闭电机 / PWM、音频、扬声器、显示屏、触摸和多路复用负载 |
| CPU | 固定为 240 MHz |

## 运行模式

固件初始化固定的 IMU 采样配置，验证传感器配置，并每 10 ms 采样一次 IMU。显示屏、触摸、Wi-Fi、音频、电机和外部 5 V 输出均关闭。

## 固定配置

此测试只有一种固定配置：使用 Context 模式，通过 AUX 启用 BMM150 磁力计；加速度计和陀螺仪均为 200 Hz，加速度计量程为 +/-4 g，陀螺仪量程为 +/-1000 dps。测试使用 `test_id=imu-sampling`、`config_id=imu-sampling` 和 100 kHz I2C，不提供可选的低功耗模式。

## 测量电流

测量结果中的电流为测量记录中的 `WINDOW Avg`。

- 3.7 V：36.32 mA
- 4.2 V：35.61 mA

[查看测量曲线](../../docs/power-graphs/README_CN.md)。

## 配置

ESP32-S3、16 MB Flash、80 MHz OPI PSRAM、240 MHz CPU、100 kHz I2C、`test_id=imu-sampling`、`config_id=imu-sampling`。

## 构建与烧录

使用 ESP-IDF 5.5.5：

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

确认 `TEST_START`、`BOARD_OK`、`PROFILE`、`TEST_READY` 和稳定的运行时采样。进行功耗测量前断开 USB Type-C。
