# Wi-Fi Periodic Traffic Test

[English](README.md) | [简体中文](README_CN.md)

## Purpose

This firmware measures whole-board input current while the Wi-Fi station remains connected and generates controlled periodic ICMP traffic.

## Enabled Modules

- M5PM1 and M5IOE1 power control
- ESP32-S3 Wi-Fi station
- Periodic ICMP traffic
- Network interface, event loop and NVS support
- Shared board, power and test support

## Module Configuration

| Module | Configuration |
|---|---|
| Wi-Fi mode | ESP32-S3 station mode, connected to one controlled AP |
| Power save | `WIFI_PS_NONE` |
| Connect timeout | `15000 ms` |
| Traffic | ICMP Echo, one packet every `1000 ms`, 64-byte payload, `500 ms` timeout |
| Connectivity preflight | `10` ICMP Echo requests; at least `9` replies required |
| Runtime logging | No per-packet log after `TEST_READY`; non-timeout runtime errors fail the test |
| AP channel during measurement | Channel `1` |
| Credentials | Build-time `CONFIG_M5SW_WIFI_SSID` and `CONFIG_M5SW_WIFI_PASSWORD` |
| Traffic target | Build-time `CONFIG_M5SW_WIFI_TARGET_IPV4` |
| Recorded measurement | RSSI `-41 dBm` and DHCP IPv4 `192.168.0.106`; actual values vary with the access point and test run |
| Peripheral state | L2 operating power configuration; L1 rail and RTC powered; display off, buttons isolated, touch and IMU asleep, audio/motor/external 5 V off |
| I2C | PM1 and M5IOE1 at 100 kHz |

## Runtime Mode

The firmware configures the board for L2 operation, connects with `WIFI_PS_NONE`, sends 10 ICMP Echo requests as a connectivity preflight, and then sends one 64-byte ICMP packet every second. Each request has a 500 ms timeout. At least 9 of the 10 preflight requests must receive replies before `TEST_READY`; periodic traffic continues without per-packet logs afterward.

## Network Configuration

Run `idf.py menuconfig`, press `/`, search for `M5SW_WIFI_SSID`, and press `Enter` to jump to the Wi-Fi configuration menu. Set `Controlled test AP SSID`, `Controlled test AP password`, and `Periodic ICMP target IPv4 address`. The target must be a fixed host on the same LAN that responds to ICMP Echo requests.

## Measured Current

The current shown in the measurement results is the `WINDOW Avg` from the measurement record.

- 3.7 V: 105.09 mA
- 4.2 V: 97.83 mA

[View the measurement graphs](../../docs/power-graphs/README.md).

## Configuration

ESP32-S3, 16 MB Flash, OPI PSRAM at 80 MHz, 240 MHz CPU, 100 kHz I2C, `test_id=wifi-periodic-traffic`, `config_id=wifi-periodic-traffic`.

## Build and Flash

Use ESP-IDF 5.5.5:

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

Confirm `TEST_START`, `BOARD_OK ... PERIOD_MS=1000 PAYLOAD_BYTES=64`, and `TEST_READY`. Disconnect USB Type-C before power measurement.
