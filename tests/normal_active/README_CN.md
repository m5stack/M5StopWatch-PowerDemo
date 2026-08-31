# 正常工作测试

[English](README.md) | [简体中文](README_CN.md)

## 目的

本固件用于测量 M5StopWatch 在典型正常工作状态下的整板输入电流，此时主要用户界面和传感外设均已初始化。

## 启用模块

- M5PM1 和 M5IOE1 电源控制
- AMOLED 显示屏
- 触摸控制器
- 两个硬件按键
- RX8130 RTC
- BMI270 IMU
- 共享板级与测试支持

## 模块配置

| 模块 | 配置 |
|---|---|
| M5PM1 | I2C 地址 `0x6E`，100 kHz；启用充电和 LDO；启用 LDO 保持；关闭状态 LED 和外部 5 V 输出 |
| M5IOE1 | I2C 地址 `0x4F`，100 kHz，关闭空闲睡眠；电机 `0%`；关闭音频电源轨和扬声器 PA |
| AMOLED | 50% 亮度；秒表画面以 1 Hz 刷新；80 MHz QSPI 面板路径 |
| CST820 触摸 | I2C 地址 `0x15`，100 kHz；复位拉低 `10 ms`，就绪延迟 `50 ms`；GPIO13 上拉输入，关闭中断 |
| 按键 | GPIO1 和 GPIO2 上拉输入；关闭中断 |
| RX8130 | I2C 地址 `0x32`，100 kHz；关闭 Alarm、定时器和更新中断 |
| BMI270 | I2C 地址 `0x68`，100 kHz；初始化 Base API；不启用任意运动唤醒 |
| CPU 和内存 | CPU 240 MHz；需要 OPI PSRAM |

## 运行模式

L1 电源域保持启用。显示屏以 1 Hz 刷新并使用 50% 亮度。触摸、按键、RTC 和 IMU 保持初始化和供电状态，其具体中断或唤醒源按照上表关闭。Wi-Fi、音频、电机和外部 5 V 输出关闭。`TEST_READY` 后，固件持续执行规定的正常工作负载。

## 测量电流

测量结果中的电流为测量记录中的 `WINDOW Avg`。

- 3.7 V：45.69 mA
- 4.2 V：44.53 mA

[查看测量曲线](../../docs/power-graphs/README_CN.md)。

## 配置

ESP32-S3、16 MB Flash、80 MHz OPI PSRAM、240 MHz CPU、100 kHz I2C、`test_id=normal-active`、`config_id=normal-active`。

## 构建与烧录

使用 ESP-IDF 5.5.5：

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

串口监视仅用于功能验证。进行功耗测量前断开 USB Type-C 并停止监视。

## 预期验证

确认 `TEST_START`、`BOARD_OK PM1=1 IOE1=1 L1=on DISPLAY=50 TP=1 BUTTON=2 RTC=1 IMU=1` 和 `TEST_READY`。运行时故障必须报告 `TEST_FAIL`，并安全关闭所有负载。
