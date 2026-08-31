# Wi-Fi 周期流量测试

[English](README.md) | [简体中文](README_CN.md)

## 目的

本固件用于测量 Wi-Fi STA 保持连接并产生受控周期 ICMP 流量时的整板输入电流。

## 启用模块

- M5PM1 和 M5IOE1 电源控制
- ESP32-S3 Wi-Fi STA
- 周期 ICMP 流量
- 网络接口、事件循环和 NVS 支持
- 共享板级、电源与测试支持

## 模块配置

| 模块 | 配置 |
|---|---|
| Wi-Fi 模式 | ESP32-S3 STA 模式，连接到一个受控 AP |
| 省电模式 | `WIFI_PS_NONE` |
| 连接超时 | `15000 ms` |
| 流量 | ICMP Echo，每 `1000 ms` 一个数据包，64 字节负载，`500 ms` 超时 |
| 连通性预检 | 发送 `10` 次 ICMP Echo 请求；至少收到 `9` 次响应 |
| 运行时日志 | `TEST_READY` 后不输出逐包日志；非超时类运行错误会使测试失败 |
| 测量时的 AP 信道 | 信道 `1` |
| 凭据 | 构建时配置 `CONFIG_M5SW_WIFI_SSID` 和 `CONFIG_M5SW_WIFI_PASSWORD` |
| 流量目标 | 构建时配置 `CONFIG_M5SW_WIFI_TARGET_IPV4` |
| 测量记录 | RSSI `-41 dBm`，DHCP IPv4 地址为 `192.168.0.106`；实际值随接入点和测试运行而变化 |
| 外设状态 | 使用 L2 工作电源配置；L1 电源轨和 RTC 保持供电；显示屏关闭、按键隔离、触摸和 IMU 休眠、音频 / 电机 / 外部 5 V 关闭 |
| I2C | PM1 和 M5IOE1 为 100 kHz |

## 运行模式

固件将主板配置为 L2 工作状态，使用 `WIFI_PS_NONE` 建立连接，发送 10 次 ICMP Echo 请求进行连通性预检，然后每秒发送一个 64 字节 ICMP 数据包。每次请求的超时时间为 500 ms。进入 `TEST_READY` 前，10 次预检请求中至少 9 次必须收到响应；之后继续产生周期流量，但不输出逐包日志。

## 网络配置

运行 `idf.py menuconfig`，按 `/`，搜索 `M5SW_WIFI_SSID`，再按 `Enter` 跳转到 Wi-Fi 配置菜单。设置 `Controlled test AP SSID`、`Controlled test AP password` 和 `Periodic ICMP target IPv4 address`。目标必须是同一局域网中响应 ICMP Echo 请求的固定主机。

## 测量电流

测量结果中的电流为测量记录中的 `WINDOW Avg`。

- 3.7 V：105.09 mA
- 4.2 V：97.83 mA

[查看测量曲线](../../docs/power-graphs/README_CN.md)。

## 配置

ESP32-S3、16 MB Flash、80 MHz OPI PSRAM、240 MHz CPU、100 kHz I2C、`test_id=wifi-periodic-traffic`、`config_id=wifi-periodic-traffic`。

## 构建与烧录

使用 ESP-IDF 5.5.5：

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

确认 `TEST_START`、`BOARD_OK ... PERIOD_MS=1000 PAYLOAD_BYTES=64` 和 `TEST_READY`。进行功耗测量前断开 USB Type-C。
