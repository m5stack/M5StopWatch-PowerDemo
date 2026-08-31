# L0 关机测试

[English](README.md) | [简体中文](README_CN.md)

## 目的

本固件用于测量 M5StopWatch 处于 L0 关机状态时的整板输入电流，并验证 PM1 关机命令和电池等效供电下的静态电流路径。

## 启用模块

- M5PM1 电源管理器
- 电池供电路径
- M5StopWatch 共享板级支持
- 测试日志和状态支持

本测试有意不初始化 RTC、BMI270、M5IOE1、显示屏、触摸、Wi-Fi、音频和电机功能。

## 模块配置

| 模块 | 配置 |
|---|---|
| M5PM1 | I2C 地址 `0x6E`，100 kHz；关闭充电和升压；关闭 LDO 保持；关闭 GPIO0 和 GPIO4 唤醒；使用 PM1 关机命令进入 L0 |
| 唤醒源 | 无 |
| 准备过程 | `TEST_READY` 后延迟 3 秒再进入 L0 |
| 关闭负载 | M5IOE1、RTC、IMU、显示屏、触摸、Wi-Fi、音频、电机和外部 5 V 输出 |

## 运行模式

固件初始化共享 I2C 总线和 PM1 驱动，报告板级就绪状态，等待 3 秒后进入 L0 关机。PM1 切断 ESP32-S3 供电前，`TEST_READY` 是最后一条预期串口消息。

## 测量电流

测量结果中的电流为测量记录中的 `WINDOW Avg`。

- 3.7 V：8.89 uA
- 4.2 V：9.08 uA

[查看测量曲线](../../docs/power-graphs/README_CN.md)。

## 配置

| 设置 | 值 |
|---|---|
| 目标 | ESP32-S3 |
| Flash | 16 MB |
| PSRAM | OPI，80 MHz |
| CPU | 240 MHz |
| I2C | 100 kHz |
| 测试 ID | `l0-shutdown` |
| 配置 ID | `l0-shutdown` |

## 构建与烧录

在此目录中使用 ESP-IDF 5.5.5：

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

串口监视仅用于功能验证。进行功耗测量时，断开 USB Type-C 且不要运行串口监视器。生成的 `build/`、`sdkconfig` 和 `dependencies.lock` 文件属于本地产物。

## 预期验证

确认 `TEST_START`、`BOARD_OK PM1=1` 和 `TEST_READY`。关机后主机侧串口断开属于预期现象，本身不表示固件失败。
