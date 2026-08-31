# RTC Timer Wake L2 Test

[English](README.md) | [简体中文](README_CN.md)

## Purpose

This firmware validates RX8130 countdown-timer wake from ESP32-S3 Deep Sleep and measures whole-board input current while the device waits in Deep Sleep.

## Enabled Modules

- M5PM1 power manager and GPIO0 wake route
- M5IOE1 L2 low-power state configuration
- RX8130 countdown timer
- BMI270 status checks for ruling out non-target wake events
- ESP32-S3 deep sleep and `ext0` wake
- Shared wake and test support

## Module Configuration

| Module | Configuration |
|---|---|
| M5PM1 | I2C address `0x6E`, 100 kHz; DCDC and LDO enabled; watchdog timeout `0`; GPIO0 falling-edge wake; GPIO1 aggregate IRQ output; PM1 I2C idle sleep intentionally disabled |
| M5IOE1 | I2C address `0x4F`, 100 kHz; external loads disabled; PWM stopped; idle-sleep time `1 s` |
| RX8130 | I2C address `0x32`, 100 kHz; one-shot countdown timer `120 s`, 1 Hz timer source |
| BMI270 | any-motion detection disabled; retained status checked to rule out non-target wake events |
| ESP32-S3 wake | `ext0` on GPIO12, active-low |
| Wake path | RX8130 IRQ -> PM1 GPIO0 -> PM1 GPIO1 -> ESP32 GPIO12 |
| Acceptance | ESP `ext0`, PM1 GPIO0 and RTC timer status must agree; the RTC alarm and IMU any-motion flags must remain clear |

## Runtime Mode

The firmware prepares the L2 state, configures a one-shot 120-second RX8130 timer, arms the PM1 GPIO0 and ESP32 GPIO12 wake chain, reports readiness and enters Deep Sleep. PM1 I2C idle sleep is intentionally disabled for this path because the RX8130 timer wake signal is a short IRQ pulse. After wake, the firmware verifies the ESP, PM1 and RTC status and clears the recorded status flags.

## Measured Current

The current shown in the measurement results is the `WINDOW Avg` from the measurement record.

- 3.7 V: 630.98 uA
- 4.2 V: 622.45 uA

[View the measurement graphs](../../docs/power-graphs/README.md).

## Configuration

ESP32-S3, 16 MB Flash, OPI PSRAM at 80 MHz, 240 MHz CPU and 100 kHz I2C. Use ESP-IDF 5.5.5.

## Build and Flash

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

Expected logs include `RTC_CONFIG_OK`, `BOARD_OK`, `WAKE_ARMED`, `TEST_READY`, `WAKE_CAPTURE`, `WAKE_CLEAR_OK` and `TEST_PASS`. Disconnect USB Type-C before power measurement.
