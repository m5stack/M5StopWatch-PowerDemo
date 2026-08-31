# 显示负载测试

[English](README.md) | [简体中文](README_CN.md)

## 目的

本固件用于测量启用 AMOLED、关闭其他可选外设负载时的整板输入电流。

## 启用模块

- M5PM1 电源管理器
- M5IOE1 显示控制路径
- M5GFX AMOLED 显示驱动
- 共享板级与测试支持

## 模块配置

| 模块 | 配置 |
|---|---|
| M5PM1 | I2C 地址 `0x6E`，100 kHz；关闭 L1 电源轨、状态 LED 和外部 5 V 输出 |
| M5IOE1 | I2C 地址 `0x4F`，100 kHz，关闭空闲睡眠；关闭电机、音频、扬声器、触摸复位和多路复用输出 |
| AMOLED | 100% 亮度；显示一帧全屏白色画面；`TEST_READY` 后不刷新；80 MHz QSPI 面板路径 |
| CPU 和内存 | CPU 240 MHz；启用 OPI PSRAM |
| 关闭模块 | 触摸、RTC、IMU、Wi-Fi、音频、电机和外部 5 V 输出 |

## 运行模式

固件以 100% 亮度启用显示屏并绘制白色画面。CPU 频率固定为 240 MHz。L1 电源域、触摸控制器、RTC、IMU、Wi-Fi、音频、电机和外部 5 V 输出均关闭。`TEST_READY` 后保持静态显示状态，不再进行 I2C 活动。

## 测量电流

测量结果中的电流为测量记录中的 `WINDOW Avg`。

- 3.7 V：154.45 mA
- 4.2 V：144.24 mA

[查看测量曲线](../../docs/power-graphs/README_CN.md)。

## 配置

ESP32-S3、16 MB Flash、80 MHz OPI PSRAM、240 MHz CPU、100 kHz I2C、`test_id=display-only`、`config_id=display-only`。

## 构建与烧录

使用 ESP-IDF 5.5.5：

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

串口监视器仅用于功能验证。进行功耗测量前断开 USB Type-C。

## 预期验证

确认 `TEST_START`、`BOARD_OK PM1=1 IOE1=1 DISPLAY=100 WHITE=1` 和 `TEST_READY`。就绪后不应出现周期诊断日志。
