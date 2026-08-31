# IMU 唤醒 L2 测试

[English](README.md) | [简体中文](README_CN.md)

## 目的

本固件用于验证 BMI270 任意运动检测事件通过 PM1 GPIO0 和 ESP32 `ext0` 路径，从 ESP32-S3 Deep Sleep 中唤醒设备，并测量设备等待唤醒期间的整板输入电流。

## 启用模块

- M5PM1 电源管理器和 GPIO0 唤醒路径
- M5IOE1 低功耗状态配置
- 用于排除非目标唤醒事件的 RX8130 RTC 状态检查
- BMI270 IMU 任意运动检测引擎
- ESP32-S3 深度睡眠和 `ext0` 唤醒
- 共享唤醒与测试支持

## 模块配置

| 模块 | 配置 |
|---|---|
| M5PM1 | I2C 地址 `0x6E`，100 kHz；启用 DCDC 和 LDO；看门狗超时 `0`；GPIO0 下降沿唤醒；GPIO1 聚合 IRQ 输出；I2C 空闲睡眠时间 `1 s` |
| M5IOE1 | I2C 地址 `0x4F`，100 kHz；关闭外部负载；停止 PWM；空闲睡眠时间 `1 s` |
| BMI270 | I2C 地址 `0x68`，100 kHz，Base API；任意运动检测阈值 `30`、持续时间 `10`；INT1 高电平有效、推挽、非锁存 |
| 完整功能模式 | 加速度计 +/-2 g、100 Hz，陀螺仪 +/-2000 dps、100 Hz；启用加速度计、陀螺仪、佩戴唤醒和任意运动检测 |
| 低功耗模式 | 加速度计 +/-2 g、50 Hz，功耗优化 AVG1 模式；启用加速度计和任意运动检测；关闭陀螺仪和佩戴唤醒 |
| RX8130 | 关闭中断源，检查保留的状态标志以排除非目标 RTC 唤醒事件 |
| ESP32-S3 唤醒 | GPIO12 上的 `ext0`，低电平有效 |
| 唤醒路径 | BMI270 INT1 -> PM1 GPIO0 -> PM1 GPIO1 -> ESP32 GPIO12 |
| 准备过程 | 应用延迟 3 秒，随后执行最后 1 秒的 L2 稳定等待 |

## 运行模式

固件读取并清除保留的状态标志，将外设切换到规定的 L2 低功耗状态，应用选定的 IMU 模式，启用 PM1 GPIO0 和 ESP32 GPIO12 唤醒链路，报告就绪状态，等待 3 秒后进入 Deep Sleep。唤醒后，固件验证 ESP 唤醒原因、PM1 GPIO IRQ 状态和 BMI270 任意运动检测状态，随后清除已记录的状态标志并报告结果。

## 工作模式

在 `main/main.cpp` 中通过 `M5SW_IMU_WAKE_L2_REFERENCE_PROFILE` 选择模式：

- `1`、`reference`：启用加速度计、陀螺仪、佩戴唤醒和任意运动检测。
- `0`、`low_power`：加速度计以任意运动检测要求的 50 Hz 速率运行在功耗优化 AVG1 模式；仅启用加速度计和任意运动检测，关闭陀螺仪和佩戴唤醒。

两种模式均使用 `test_id=imu-wake-l2` 和 `config_id=imu-wake-l2`。

## 测量电流

测量结果中的电流为测量记录中的 `WINDOW Avg`。

| 模式 | 3.7 V | 4.2 V |
|---|---:|---:|
| 完整功能模式 | 708.35 uA | 694.76 uA |
| 低功耗模式 | 73.91 uA | 60.50 uA |

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

预期日志包括 `BOARD_OK`、`WAKE_ARMED`、`TEST_READY`、`WAKE_CAPTURE`、`WAKE_CLEAR_OK` 和 `TEST_PASS`。进行功耗测量前断开 USB Type-C。
