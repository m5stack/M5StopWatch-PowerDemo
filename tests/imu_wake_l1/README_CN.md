# IMU 唤醒 L1 测试

[English](README.md) | [简体中文](README_CN.md)

## 目的

本固件用于验证主板处于 L1 待机时，BMI270 任意运动检测事件通过 PM1 GPIO0 路径唤醒设备，并测量设备等待唤醒期间的整板输入电流。

## 启用模块

- M5PM1 电源管理器
- 用于排除非目标唤醒和清除状态的 RX8130 RTC
- BMI270 IMU 任意运动检测引擎
- PM1 GPIO0 唤醒路径
- 共享唤醒与测试支持

## 模块配置

| 模块 | 配置 |
|---|---|
| M5PM1 | I2C 地址 `0x6E`，100 kHz；启用 LDO 保持；GPIO0 配置为带上拉的输入和下降沿唤醒；关闭 GPIO4 唤醒 |
| BMI270 | I2C 地址 `0x68`，100 kHz，Base API；任意运动检测阈值 `30`、持续时间 `10`；INT1 高电平有效、推挽、非锁存 |
| 完整功能模式 | 加速度计 +/-2 g、100 Hz，陀螺仪 +/-2000 dps、100 Hz；启用加速度计、陀螺仪、佩戴唤醒和任意运动检测 |
| 低功耗模式 | 加速度计 +/-2 g、50 Hz，功耗优化 AVG1 模式；启用加速度计和任意运动检测；关闭陀螺仪和佩戴唤醒 |
| RX8130 | I2C 地址 `0x32`，100 kHz；关闭 Alarm、定时器和更新中断，用于排除非目标 RTC 唤醒事件 |
| 唤醒路径 | BMI270 INT1 -> PM1 GPIO0 -> PM1 L1 唤醒 |
| 准备过程 | `TEST_READY` 后延迟 3 秒再进入 L1 |

## 运行模式

固件读取 RTC 和 IMU 中保留的状态标志，清除残留事件，关闭 RTC 中断，配置 BMI270 INT1 和 PM1 GPIO0，报告就绪状态，等待 3 秒后进入 L1 待机。发生运动唤醒后，固件验证 PM1 和 BMI270 状态，排除非目标 RTC 唤醒事件，清除已记录的状态标志并报告结果。

## 工作模式

通过 `main/main.cpp` 顶部附近的 `M5SW_IMU_WAKE_L1_REFERENCE_PROFILE` 选择模式：

- `1`、`reference`：启用加速度计、陀螺仪、佩戴唤醒和任意运动检测。
- `0`、`low_power`：加速度计以任意运动检测要求的 50 Hz 速率运行在功耗优化 AVG1 模式；仅启用加速度计和任意运动检测，关闭陀螺仪和佩戴唤醒。

两种模式均保持 `test_id=imu-wake-l1` 和 `config_id=imu-wake-l1`。

## 测量电流

测量结果中的电流为测量记录中的 `WINDOW Avg`。

| 模式 | 3.7 V | 4.2 V |
|---|---:|---:|
| 完整功能模式 | 670.94 uA | 671.55 uA |
| 低功耗模式 | 37.56 uA | 39.17 uA |

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

确认 `TEST_START`、`BOARD_OK`、`WAKE_ARMED`、`TEST_READY`、`WAKE_CAPTURE`、`WAKE_CLEAR_OK` 和 `TEST_PASS`。进行功耗测量前断开 USB Type-C。
