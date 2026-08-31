# Full Load Test

[English](README.md) | [简体中文](README_CN.md)

## Purpose

This firmware measures whole-board input current in the defined high-load operating state. The display, audio and motor loads run continuously while the Wi-Fi subsystem remains initialized without connecting or scanning.

## Enabled Modules

- M5PM1 and M5IOE1 power control
- AMOLED display and M5GFX
- Touch controller
- ESP32-S3 Wi-Fi station initialization
- ES8311 audio codec and I2S TX/RX
- Motor PWM
- Shared board, power and test support

## Module Configuration

| Module | Configuration |
|---|---|
| M5PM1 | I2C `400 kHz`; charger and LDO enabled; LDO hold enabled; status LED and external 5 V output disabled |
| M5IOE1 | I2C `400 kHz`, idle sleep disabled; L3B/display and audio rails enabled |
| AMOLED | 50% brightness (`128/255`); 466 active lines; automatic framebuffer; continuous random drawing without frame-rate limiting |
| Touch | CST820 at `0x15`, 100 kHz; reset low `10 ms`, ready delay `50 ms`; GPIO13 input |
| Wi-Fi | Station initialized with `WIFI_PS_MIN_MODEM`; no connection and no scan |
| Audio | ES8311 at `0x18`, I2C `400 kHz`; 44.1 kHz, 16-bit stereo, I2S TX/RX, codec input/output mode, 30 dB input gain, 100% output volume, test tone at approximately 54% of full-scale RMS |
| Motor | PWM channel 0, `5 kHz`, `100%` duty |
| RTOS | Tick rate `1000 Hz`; display task stack `4096` bytes; audio task stack `6144` bytes |
| Stop conditions | GPIO1 right-key normal stop; `600 s` maximum runtime; `5 s` worker-task stop timeout |
| Safety gate | `CONFIG_M5SW_FULL_LOAD_SAFETY_ACK=y` required before running |

## Runtime Mode

The L1 power domain and touch controller remain enabled. The display uses a brightness level of 128/255, 466 active lines, an automatic framebuffer and continuous random drawing without frame-rate limiting. Wi-Fi is initialized with `WIFI_PS_MIN_MODEM` but is not connected and does not scan. Audio runs at 44.1 kHz with 16-bit stereo TX/RX, codec input/output mode, 30 dB input gain and 100% output volume. The motor runs at a 5 kHz frequency and 100% duty cycle. The PM1, IOE1, display and audio control paths use 400 kHz I2C; touch remains at 100 kHz.

## Safety Gate

`CONFIG_M5SW_FULL_LOAD_SAFETY_ACK` is disabled by default. Do not flash or run this firmware until the fixture rating, supply current limit, external temperature limit and manual stop procedure have been reviewed and approved. The firmware also enforces a 600-second maximum runtime and a safe load-shutdown mechanism.

## Measured Current

The current shown in the measurement results is the `WINDOW Avg` from the measurement record.

- 3.7 V: 285.77 mA
- 4.2 V: 276.32 mA

[View the measurement graphs](../../docs/power-graphs/README.md).

## Configuration

ESP32-S3, 16 MB Flash, OPI PSRAM at 80 MHz, 240 MHz CPU, `test_id=full-load`, `config_id=full-load`.

## Build and Flash

Use ESP-IDF 5.5.5:

```bash
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

Enable the safety acknowledgement only after the documented safety review is approved. Confirm `TEST_START`, `BOARD_OK ... DISPLAY_PERCENT=50 DISPLAY_RAW=128 WIFI=initialized WIFI_PS=min_modem WIFI_SCAN=off`, `TEST_READY` and the final `TEST_PASS` or `TEST_FAIL`. Disconnect USB Type-C before power measurement.
