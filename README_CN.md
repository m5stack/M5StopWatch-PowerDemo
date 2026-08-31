# M5StopWatch 功耗演示

[English](README.md) | [简体中文](README_CN.md)

## 项目概述

本仓库包含多个相互独立的 ESP-IDF 固件项目，用于测量量产 M5StopWatch 硬件的功耗。每项测试独立构建和烧录，上电后自动启动，并复用共享的板级支持模块。

项目基于 ESP-IDF 5.5.5。

> **满载测试安全门槛：**在夹具额定值、电流限制、外部温度限制和手动停止流程完成审核前，请勿烧录或运行 `tests/full_load`。`CONFIG_M5SW_FULL_LOAD_SAFETY_ACK` 默认关闭。

## 功耗测试结果

本文以整板平均输入电流表示功耗。每项测量结果均采用约 60 秒测量窗口中的 `WINDOW Avg`。此处仅列出器件的简要工作状态，完整配置请查看详情链接。

| 测试场景 | 器件 / 工作状态 | 3.7 V<br>平均电流 | 4.2 V<br>平均电流 | 详情 |
|---|---|---:|---:|---|
| L0 关机 | M5PM1：L0 关机<br>RX8130：备份域保持供电 | 8.89 uA | 9.08 uA | [详情](tests/l0_shutdown/README_CN.md) |
| L1 待机 | M5PM1：L1 待机<br>BMI270：保持供电，关闭任意运动检测<br>RX8130：保持供电，关闭中断 | 20.49 uA | 20.67 uA | [详情](tests/l1_standby/README_CN.md) |
| L2 深度睡眠 | ESP32-S3：Deep Sleep<br>M5PM1：L2 电源配置，启用 I2C 空闲睡眠<br>BMI270：挂起 | 90.06 uA | 51.65 uA | [详情](tests/l2_deepsleep/README_CN.md) |
| IMU 唤醒 L1 完整功能模式 | M5PM1：L1 待机，启用 GPIO0 唤醒<br>BMI270：加速度计 + 陀螺仪 + 佩戴唤醒 + 任意运动检测<br>RX8130：保持供电，关闭中断 | 670.94 uA | 671.55 uA | [详情](tests/imu_wake_l1/README_CN.md) |
| IMU 唤醒 L1 低功耗模式 | M5PM1：L1 待机，启用 GPIO0 唤醒<br>BMI270：加速度计 + 任意运动检测<br>RX8130：保持供电，关闭中断 | 37.56 uA | 39.17 uA | [详情](tests/imu_wake_l1/README_CN.md) |
| IMU 唤醒 L2 完整功能模式 | ESP32-S3：Deep Sleep<br>M5PM1：启用 GPIO0 唤醒和 I2C 空闲睡眠<br>BMI270：加速度计 + 陀螺仪 + 佩戴唤醒 + 任意运动检测 | 708.35 uA | 694.76 uA | [详情](tests/imu_wake_l2/README_CN.md) |
| IMU 唤醒 L2 低功耗模式 | ESP32-S3：Deep Sleep<br>M5PM1：启用 GPIO0 唤醒和 I2C 空闲睡眠<br>BMI270：加速度计 + 任意运动检测 | 73.91 uA | 60.50 uA | [详情](tests/imu_wake_l2/README_CN.md) |
| RTC Alarm 唤醒 L1 | M5PM1：L1 待机，启用 GPIO0 唤醒<br>RX8130：Alarm 已启用<br>BMI270：关闭任意运动检测 | 20.44 uA | 20.62 uA | [详情](tests/rtc_alarm_wake_l1/README_CN.md) |
| RTC Alarm 唤醒 L2 | ESP32-S3：Deep Sleep<br>M5PM1：启用 GPIO0 唤醒和 I2C 空闲睡眠<br>RX8130：Alarm 已启用<br>BMI270：关闭任意运动检测 | 57.06 uA | 43.44 uA | [详情](tests/rtc_alarm_wake_l2/README_CN.md) |
| RTC 定时器唤醒 L1 | M5PM1：L1 待机，启用 GPIO0 唤醒<br>RX8130：定时器已启用<br>BMI270：关闭任意运动检测 | 20.46 uA | 20.52 uA | [详情](tests/rtc_time_wake_l1/README_CN.md) |
| RTC 定时器唤醒 L2 | ESP32-S3：Deep Sleep<br>M5PM1：启用 GPIO0 唤醒，关闭 I2C 空闲睡眠<br>RX8130：定时器已启用<br>BMI270：关闭任意运动检测 | 630.98 uA | 622.45 uA | [详情](tests/rtc_time_wake_l2/README_CN.md) |
| 正常工作 | ESP32-S3：运行<br>AMOLED：50% 亮度，1 Hz 刷新<br>BMI270：已初始化，关闭任意运动检测<br>RX8130：保持供电，关闭中断<br>CST820：已初始化，关闭中断 | 45.69 mA | 44.53 mA | [详情](tests/normal_active/README_CN.md) |
| 显示负载 | ESP32-S3：运行<br>AMOLED：100% 亮度，静态白屏<br>CST820：关闭<br>RX8130：关闭<br>BMI270：关闭<br>Wi-Fi：关闭 | 154.45 mA | 144.24 mA | [详情](tests/display_only/README_CN.md) |
| IMU 采样 | ESP32-S3：运行<br>BMI270：持续采样加速度计、陀螺仪和 AUX<br>AMOLED：关闭<br>Wi-Fi：关闭<br>ES8311：关闭 | 36.32 mA | 35.61 mA | [详情](tests/imu_sampling/README_CN.md) |
| Wi-Fi 空闲连接 | ESP32-S3：运行<br>Wi-Fi：保持连接，省电模式为 `WIFI_PS_NONE`<br>AMOLED：关闭<br>BMI270：关闭<br>ES8311：关闭 | 105.73 mA | 98.67 mA | [详情](tests/wifi_connected/README_CN.md) |
| Wi-Fi Light Sleep | ESP32-S3：自动 Light Sleep<br>Wi-Fi：保持连接，省电模式为 `WIFI_PS_MAX_MODEM`<br>AMOLED：关闭<br>BMI270：关闭<br>ES8311：关闭 | 3.49 mA | 3.43 mA | [详情](tests/wifi_light_sleep/README_CN.md) |
| Wi-Fi 周期流量 | ESP32-S3：运行<br>Wi-Fi：保持连接，省电模式为 `WIFI_PS_NONE`<br>ICMP 流量：每秒发送一个 64 字节数据包<br>AMOLED：关闭<br>BMI270：关闭<br>ES8311：关闭 | 105.09 mA | 97.83 mA | [详情](tests/wifi_periodic_traffic/README_CN.md) |
| 满载 | ESP32-S3：运行<br>AMOLED：50% 亮度，持续随机绘制<br>ES8311：持续播放测试音<br>I2S：持续传输音频流<br>电机：5 kHz，100% 占空比<br>Wi-Fi：已初始化，不连接且不扫描<br>CST820：已初始化 | 285.77 mA | 276.32 mA | [详情](tests/full_load/README_CN.md) |

L0 不启用或测试 RTC 功能。RX8130 备份域仍保持物理连接，并计入整板测量电流。

## 测量曲线

所有 3.7 V 和 4.2 V 曲线统一收录在[功耗测量曲线](docs/power-graphs/README_CN.md)索引中。

## 仓库结构

```text
components/  第三方库和固定源码快照
modules/     M5StopWatch 共享板级与测试支持组件
tests/       相互独立的 ESP-IDF 固件项目
```

## 构建与烧录

安装并激活 ESP-IDF 5.5.5，然后初始化仓库依赖：

```bash
git submodule update --init --recursive
```

在对应测试目录中构建和烧录单项测试：

```bash
cd tests/l0_shutdown
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

每个测试目录都有独立的 `sdkconfig.defaults`。所需的第三方 ESP-IDF 组件已经包含或固定在本仓库中，标准构建不需要生成 `dependencies.lock`。

## IMU 唤醒模式

`tests/imu_wake_l1` 和 `tests/imu_wake_l2` 在同一份固件源码中分别提供完整功能模式和低功耗模式。构建前修改 `main/main.cpp` 顶部对应的 `M5SW_IMU_WAKE_L1_REFERENCE_PROFILE` 或 `M5SW_IMU_WAKE_L2_REFERENCE_PROFILE` 宏进行选择：

```bash
# 0：50 Hz 加速度计 + 任意运动检测配置
# 1：加速度计 + 陀螺仪 + 佩戴唤醒 + 任意运动检测配置
idf.py build
```

固件在 `BOARD_OK` 中报告 `profile_id=imu-wake-l1` 或 `profile_id=imu-wake-l2`。`PROFILE=low_power` 和 `PROFILE=reference` 字段用于标识两种模式；两种模式使用相同的项目标识、sdkconfig 和构建命令。

## 测量方法

1. 连接 USB Type-C 烧录固件，并通过串口监视器确认 `TEST_START`、`BOARD_OK` 和 `TEST_READY`。
2. 保持固件和配置不变，断开 USB Type-C，仅通过测量装置为设备供电。
3. 分别在 3.7 V 和 4.2 V 下使用约 60 秒的 `WINDOW` 进行测量。

对于包含多种模式的测试，不带 `-low` 的曲线文件名表示完整功能模式，`-low` 后缀表示低功耗模式。只有一种模式的测试不使用模式后缀。

固件不会检测供电来源，也不会在功能验证模式和功耗测量模式之间切换。

## 依赖

- [M5GFX](https://github.com/m5stack/M5GFX)，版本 `d91077b9a607b59404e4e4a49f775c792bfae382`
- [M5PM1](https://github.com/m5stack/M5PM1)，版本 `be9a5456c007c333e7ac963f33bfde1ffa5d82ee`
- [M5IOE1](https://github.com/m5stack/M5IOE1)，版本 `846eec7d05e25c09013be2acdb8804487f48a62e`
- [ic_rx8130](https://github.com/Ocean-lhy/ic_rx8130)，版本 `d79bf99aa25168ab26886d0e94cf525be5a1343e`
- `otool_bmi270`，源码快照位于 `components/otool_bmi270`
- `esp_codec_dev` 1.6.2，源码位于 `components/espressif__esp_codec_dev`

依赖版本和许可证汇总见[第三方声明](THIRD_PARTY_NOTICES_CN.md)。

## 许可证

项目自有源代码采用 [MIT License](LICENSE)。第三方组件继续遵循[第三方声明](THIRD_PARTY_NOTICES_CN.md)中列出的许可证。
