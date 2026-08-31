# Wi-Fi Light Sleep Test

[English](README.md) | [简体中文](README_CN.md)

## Purpose

This firmware measures whole-board input current while the Wi-Fi station remains connected and ESP32-S3 automatic Light Sleep is enabled.

## Enabled Modules

- M5PM1 and M5IOE1 power control
- ESP32-S3 Wi-Fi station
- Dynamic frequency scaling and automatic Light Sleep
- Network interface, event loop and NVS support
- Shared board, power and test support

## Module Configuration

| Module | Configuration |
|---|---|
| Wi-Fi mode | ESP32-S3 station mode, connected to one controlled AP |
| Power save | `WIFI_PS_MAX_MODEM` |
| Listen interval | `3` beacons |
| Connect timeout | `15000 ms` |
| ESP power management | Automatic Light Sleep enabled; tickless idle enabled; CPU range 40 ~ 240 MHz |
| Application traffic | None |
| Scan after connection | Disabled |
| AP channel during measurement | Channel `1` |
| Credentials | Build-time `CONFIG_M5SW_WIFI_SSID` and `CONFIG_M5SW_WIFI_PASSWORD` |
| Recorded measurement | RSSI `-41 dBm` and DHCP IPv4 `192.168.0.106`; actual values vary with the access point and test run |
| Peripheral state | L2 operating power configuration; L1 rail and RTC powered; display off, buttons isolated, touch and IMU asleep, audio/motor/external 5 V off |
| I2C | PM1 and M5IOE1 at 100 kHz |

## Runtime Mode

The firmware configures the board for L2 operation, sets dynamic frequency scaling to 40 ~ 240 MHz, enables automatic Light Sleep and tickless idle, sets `WIFI_PS_MAX_MODEM`, connects to the configured AP and maintains the connection without application traffic. The listen interval is fixed at 3 beacons.

## Network Configuration

Run `idf.py menuconfig`, press `/`, search for `M5SW_WIFI_SSID`, and press `Enter` to jump to the Wi-Fi configuration menu. Set `Controlled test AP SSID` and `Controlled test AP password`.

## Measured Current

The current shown in the measurement results is the `WINDOW Avg` from the measurement record.

- 3.7 V: 3.49 mA
- 4.2 V: 3.43 mA

[View the measurement graphs](../../docs/power-graphs/README.md).

## Configuration

ESP32-S3, 16 MB Flash, OPI PSRAM at 80 MHz, CPU range 40 ~ 240 MHz, `CONFIG_PM_ENABLE=y`, 100 kHz I2C, `test_id=wifi-light-sleep`, `config_id=wifi-light-sleep`.

## Build and Flash

Use ESP-IDF 5.5.5:

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

Confirm `TEST_START`, `BOARD_OK ... PS=max_modem LIGHT_SLEEP=1`, and `TEST_READY`. Disconnect USB Type-C before power measurement.
