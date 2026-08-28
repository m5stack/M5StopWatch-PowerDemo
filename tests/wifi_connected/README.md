# Wi-Fi Connected Test

## Purpose

This firmware measures a stable Wi-Fi station connection without application traffic.

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
| Test access point | Same controlled AP used by all three Wi-Fi tests |
| Test AP channel | Channel `1` in the supplied connection capture |
| Credentials | Build-time `CONFIG_M5SW_WIFI_SSID` and `CONFIG_M5SW_WIFI_PASSWORD` |
| AP fields to record | Beacon interval and DTIM period for the controlled access point |
| Reference observation | One reference run recorded RSSI `-41 dBm` and DHCP IPv4 `192.168.0.106`; actual values vary with the access point and test run |
| Per-run fields | Actual channel, RSSI and DHCP IPv4 address captured after connection; channel must be `1-14` |
| Power baseline | L2-active; L1 retained; display off, buttons isolated, touch asleep, IMU asleep, RTC retained, audio/motor/external 5 V off |
| I2C | PM1 and M5IOE1 at 100 kHz |

## Runtime Mode

The firmware prepares the L2-active baseline, initializes Wi-Fi, disables Wi-Fi power save with `WIFI_PS_NONE`, connects to the configured access point and holds the connection. Display, buttons, touch, IMU, audio, motor and external 5 V output are disabled; RTC is retained.

## Network Configuration

Run `idf.py menuconfig`, press `/`, search for `M5SW_WIFI_SSID`, and press `Enter` to jump to the Wi-Fi configuration menu. Set `Controlled test AP SSID` and `Controlled test AP password`. Use the same controlled AP for all three Wi-Fi tests. Record the AP channel, RSSI, beacon interval and DTIM period for each run.

## Power

The reported current is the `WINDOW Avg` from the selected capture.

- 3.7 V: 105.73 mA
- 4.2 V: 98.67 mA

![Wi-Fi connected, 3.7 V](../../docs/power-graphs/wifi_connected/3.7v.png)
![Wi-Fi connected, 4.2 V](../../docs/power-graphs/wifi_connected/4.2v.png)

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

Confirm `TEST_START`, `BOARD_OK ... WIFI=connected PS=none` and `TEST_READY`. Disconnect Type-C before Stage B measurement.
