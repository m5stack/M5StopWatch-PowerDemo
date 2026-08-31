# Wi-Fi Idle Connection Test

[English](README.md) | [简体中文](README_CN.md)

## Purpose

This firmware measures whole-board input current while the Wi-Fi station maintains a stable connection without application traffic.

## Enabled Modules

- M5PM1 and M5IOE1 power control
- ESP32-S3 Wi-Fi station
- Network interface, event loop and NVS support
- Shared board, power and test support

## Module Configuration

| Module | Configuration |
|---|---|
| Wi-Fi mode | ESP32-S3 station mode, connected to one controlled AP |
| Power save | `WIFI_PS_NONE` |
| Connect timeout | `15000 ms` |
| Application traffic | None |
| Scan after connection | Disabled |
| AP channel during measurement | Channel `1` |
| Credentials | Build-time `CONFIG_M5SW_WIFI_SSID` and `CONFIG_M5SW_WIFI_PASSWORD` |
| Recorded measurement | RSSI `-41 dBm` and DHCP IPv4 `192.168.0.106`; actual values vary with the access point and test run |
| Peripheral state | L2 operating power configuration; L1 rail and RTC powered; display off, buttons isolated, touch and IMU asleep, audio/motor/external 5 V off |
| I2C | PM1 and M5IOE1 at 100 kHz |

## Runtime Mode

The firmware configures the board for L2 operation, initializes Wi-Fi, disables Wi-Fi power save with `WIFI_PS_NONE`, connects to the configured access point and maintains the connection. The display, buttons, touch, IMU, audio, motor and external 5 V output are disabled; the L1 rail and RTC remain powered.

## Network Configuration

Run `idf.py menuconfig`, press `/`, search for `M5SW_WIFI_SSID`, and press `Enter` to jump to the Wi-Fi configuration menu. Set `Controlled test AP SSID` and `Controlled test AP password`.

## Measured Current

The current shown in the measurement results is the `WINDOW Avg` from the measurement record.

- 3.7 V: 105.73 mA
- 4.2 V: 98.67 mA

[View the measurement graphs](../../docs/power-graphs/README.md).

## Configuration

ESP32-S3, 16 MB Flash, OPI PSRAM at 80 MHz, 240 MHz CPU, 100 kHz I2C, `test_id=wifi-connected`, `config_id=wifi-connected`.

## Build and Flash

Use ESP-IDF 5.5.5:

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

Confirm `TEST_START`, `BOARD_OK ... WIFI=connected PS=none` and `TEST_READY`. Disconnect USB Type-C before power measurement.
