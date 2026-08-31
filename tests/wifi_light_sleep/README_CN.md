# Wi-Fi Light Sleep 测试

[English](README.md) | [简体中文](README_CN.md)

## 目的

本固件用于测量 Wi-Fi STA 保持连接且 ESP32-S3 启用自动 Light Sleep 时的整板输入电流。

## 启用模块

- M5PM1 和 M5IOE1 电源控制
- ESP32-S3 Wi-Fi STA
- 动态频率调节和自动 Light Sleep
- 网络接口、事件循环和 NVS 支持
- 共享板级、电源与测试支持

## 模块配置

| 模块 | 配置 |
|---|---|
| Wi-Fi 模式 | ESP32-S3 STA 模式，连接到一个受控 AP |
| 省电模式 | `WIFI_PS_MAX_MODEM` |
| 监听间隔 | `3` 个 Beacon |
| 连接超时 | `15000 ms` |
| ESP 电源管理 | 启用自动 Light Sleep；启用 Tickless Idle；CPU 范围 40 ~ 240 MHz |
| 应用流量 | 无 |
| 连接后扫描 | 关闭 |
| 测量时的 AP 信道 | 信道 `1` |
| 凭据 | 构建时配置 `CONFIG_M5SW_WIFI_SSID` 和 `CONFIG_M5SW_WIFI_PASSWORD` |
| 测量记录 | RSSI `-41 dBm`，DHCP IPv4 地址为 `192.168.0.106`；实际值随接入点和测试运行而变化 |
| 外设状态 | 使用 L2 工作电源配置；L1 电源轨和 RTC 保持供电；显示屏关闭、按键隔离、触摸和 IMU 休眠、音频 / 电机 / 外部 5 V 关闭 |
| I2C | PM1 和 M5IOE1 为 100 kHz |

## 运行模式

固件将主板配置为 L2 工作状态，将动态频率调节范围设为 40 ~ 240 MHz，启用自动 Light Sleep 和 Tickless Idle，设置 `WIFI_PS_MAX_MODEM`，连接到配置的 AP，并在没有应用流量时保持连接。监听间隔固定为 3 个 Beacon。

## 网络配置

运行 `idf.py menuconfig`，按 `/`，搜索 `M5SW_WIFI_SSID`，再按 `Enter` 跳转到 Wi-Fi 配置菜单。设置 `Controlled test AP SSID` 和 `Controlled test AP password`。

## 测量电流

测量结果中的电流为测量记录中的 `WINDOW Avg`。

- 3.7 V：3.49 mA
- 4.2 V：3.43 mA

[查看测量曲线](../../docs/power-graphs/README_CN.md)。

## 配置

ESP32-S3、16 MB Flash、80 MHz OPI PSRAM、CPU 范围 40 ~ 240 MHz、`CONFIG_PM_ENABLE=y`、100 kHz I2C、`test_id=wifi-light-sleep`、`config_id=wifi-light-sleep`。

## 构建与烧录

使用 ESP-IDF 5.5.5：

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

确认 `TEST_START`、`BOARD_OK ... PS=max_modem LIGHT_SLEEP=1` 和 `TEST_READY`。进行功耗测量前断开 USB Type-C。
