# Wi-Fi 空闲连接测试

[English](README.md) | [简体中文](README_CN.md)

## 目的

本固件用于测量 Wi-Fi STA 在没有应用流量的情况下保持稳定连接时的整板输入电流。

## 启用模块

- M5PM1 和 M5IOE1 电源控制
- ESP32-S3 Wi-Fi STA
- 网络接口、事件循环和 NVS 支持
- 共享板级、电源与测试支持

## 模块配置

| 模块 | 配置 |
|---|---|
| Wi-Fi 模式 | ESP32-S3 STA 模式，连接到一个受控 AP |
| 省电模式 | `WIFI_PS_NONE` |
| 连接超时 | `15000 ms` |
| 应用流量 | 无 |
| 连接后扫描 | 关闭 |
| 测量时的 AP 信道 | 信道 `1` |
| 凭据 | 构建时配置 `CONFIG_M5SW_WIFI_SSID` 和 `CONFIG_M5SW_WIFI_PASSWORD` |
| 测量记录 | RSSI `-41 dBm`，DHCP IPv4 地址为 `192.168.0.106`；实际值随接入点和测试运行而变化 |
| 外设状态 | 使用 L2 工作电源配置；L1 电源轨和 RTC 保持供电；显示屏关闭、按键隔离、触摸和 IMU 休眠、音频 / 电机 / 外部 5 V 关闭 |
| I2C | PM1 和 M5IOE1 为 100 kHz |

## 运行模式

固件将主板配置为 L2 工作状态，初始化 Wi-Fi，通过 `WIFI_PS_NONE` 关闭 Wi-Fi 省电，连接到配置的接入点并保持连接。显示屏、按键、触摸、IMU、音频、电机和外部 5 V 输出关闭，L1 电源轨和 RTC 保持供电。

## 网络配置

运行 `idf.py menuconfig`，按 `/`，搜索 `M5SW_WIFI_SSID`，再按 `Enter` 跳转到 Wi-Fi 配置菜单。设置 `Controlled test AP SSID` 和 `Controlled test AP password`。

## 测量电流

测量结果中的电流为测量记录中的 `WINDOW Avg`。

- 3.7 V：105.73 mA
- 4.2 V：98.67 mA

[查看测量曲线](../../docs/power-graphs/README_CN.md)。

## 配置

ESP32-S3、16 MB Flash、80 MHz OPI PSRAM、240 MHz CPU、100 kHz I2C、`test_id=wifi-connected`、`config_id=wifi-connected`。

## 构建与烧录

使用 ESP-IDF 5.5.5：

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

确认 `TEST_START`、`BOARD_OK ... WIFI=connected PS=none` 和 `TEST_READY`。进行功耗测量前断开 USB Type-C。
