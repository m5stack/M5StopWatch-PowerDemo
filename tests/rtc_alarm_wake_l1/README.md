# RTC Alarm Wake L1 Test

## Purpose

This firmware validates RX8130 calendar-alarm wake while the M5StopWatch remains in L1 standby.

## Enabled Modules

- M5PM1 power manager
- RX8130 RTC calendar alarm
- BMI270 IMU for non-target wake rejection
- PM1 GPIO0 wake path
- Shared wake and test support

## Module Configuration

| Module | Configuration |
|---|---|
| M5PM1 | I2C address `0x6E`, 100 kHz; LDO hold enabled; GPIO0 pull-up input with falling-edge wake; GPIO4 wake disabled |
| RX8130 | I2C address `0x32`, 100 kHz; known-time baseline; one-shot calendar alarm set for `120 s` after the baseline |
| BMI270 | I2C address `0x68`, 100 kHz; Any-motion disabled; status checked only to reject false triggers |
| Wake path | RX8130 IRQ -> PM1 GPIO0 -> PM1 L1 wake |
| Acceptance | RTC alarm flag and PM1 GPIO0 evidence must agree; IMU Any-motion must be false |

## Runtime Mode

The firmware prepares the L1 power state, normalizes a known RTC baseline to the alarm target, configures a one-shot alarm, disables unrelated wake sources, reports readiness, and enters L1 standby. After wake it verifies the RTC alarm event and rejects IMU-triggered false passes.

The current formal target is a 120-second alarm.

## Power

The reported current is the `WINDOW Avg` from the selected capture.

- 3.7 V: 20.44 uA
- 4.2 V: 20.62 uA

![RTC alarm wake L1, 3.7 V](../../docs/power-graphs/rtc_alarm_wake_l1/3.7v.png)
![RTC alarm wake L1, 4.2 V](../../docs/power-graphs/rtc_alarm_wake_l1/4.2v.png)

## Configuration

ESP32-S3, 16 MB Flash, OPI PSRAM at 80 MHz, 240 MHz CPU, 100 kHz I2C, `test_id=rtc-alarm-wake-l1`, `config_id=rtc-alarm-wake-l1`.

## Build and Flash

Use ESP-IDF 5.5.5:

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

Confirm `TEST_START`, `RTC_CONFIG_OK`, `BOARD_OK`, `WAKE_ARMED`, `TEST_READY`, `WAKE_CAPTURE`, `WAKE_CLEAR_OK` and `TEST_PASS`. Disconnect Type-C before formal power measurement.
