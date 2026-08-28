#include <cstdint>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "m5sw_audio.hpp"
#include "m5sw_board.hpp"
#include "m5sw_display.hpp"
#include "m5sw_ioe1.hpp"
#include "m5sw_pm1.hpp"
#include "m5sw_test_support.hpp"
#include "m5sw_wifi.hpp"
#include "sdkconfig.h"

namespace {

constexpr char kTag[]                          = "full_load";
constexpr std::uint32_t kHardTimeoutMs         = 600000;
constexpr std::uint32_t kShutdownTimeoutMs     = 5000;
constexpr std::uint32_t kDisplayTaskStackBytes = 4096;
constexpr std::uint32_t kAudioTaskStackBytes   = 6144;
constexpr std::uint16_t kMotorPwmFrequencyHz   = 5000;
constexpr std::uint32_t kReferenceI2cSpeedHz   = 400000;
constexpr std::uint8_t kTouchChipIdRegister    = 0xA7;
constexpr std::uint8_t kTouchFirmwareRegister  = 0xA9;
constexpr std::uint32_t kTouchResetLowMs       = 10;
constexpr std::uint32_t kTouchResetReadyMs     = 50;

constexpr EventBits_t kStartBit       = BIT0;
constexpr EventBits_t kStopBit        = BIT1;
constexpr EventBits_t kFailureBit     = BIT2;
constexpr EventBits_t kDisplayDoneBit = BIT3;
constexpr EventBits_t kAudioDoneBit   = BIT5;
constexpr EventBits_t kAllDoneBits    = kDisplayDoneBit | kAudioDoneBit;

static_assert(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ == 240, "Full Load requires the fixed 240 MHz CPU configuration");
static_assert(m5sw::audio::kSampleRateHz == 44100 && m5sw::audio::kBitsPerSample == 16 &&
                  m5sw::audio::kChannelCount == 2 && m5sw::audio::kOutputVolumePercent == 100,
              "Full Load requires reference 44.1 kHz, 16-bit stereo audio");
static_assert(CONFIG_FREERTOS_HZ == 1000, "Reference Full Load timing requires a 1000 Hz RTOS tick");

struct FailureRecord {
    const char* stage = nullptr;
    esp_err_t error   = ESP_OK;
    int raw_error     = 0;
};

struct FullLoadContext {
    EventGroupHandle_t events                 = nullptr;
    m5sw::display::DisplayController* display = nullptr;
    m5sw::wifi::WifiController* wifi          = nullptr;
    m5sw::audio::AudioController* audio       = nullptr;
};

portMUX_TYPE g_failure_lock = portMUX_INITIALIZER_UNLOCKED;
FailureRecord g_failure     = {};

[[noreturn]] void fail(const char* stage, esp_err_t error, int raw_error)
{
    ESP_LOGE(kTag, "BOARD_FAIL stage=%s error=%s raw=%d", stage, esp_err_to_name(error), raw_error);
    m5sw::test_support::report_final(m5sw::test_support::FinalStatus::fail, "Full Load setup failed");
    m5sw::test_support::hold_safe_idle();
}

void require(const char* stage, esp_err_t error)
{
    if (error != ESP_OK) fail(stage, error, error);
}

void record_failure(FullLoadContext* context, const char* stage, esp_err_t error, int raw_error)
{
    portENTER_CRITICAL(&g_failure_lock);
    if (g_failure.stage == nullptr) g_failure = {stage, error, raw_error};
    portEXIT_CRITICAL(&g_failure_lock);
    xEventGroupSetBits(context->events, kFailureBit | kStopBit);
    context->audio->request_stop();
}

FailureRecord failure_snapshot()
{
    portENTER_CRITICAL(&g_failure_lock);
    const FailureRecord snapshot = g_failure;
    portEXIT_CRITICAL(&g_failure_lock);
    return snapshot;
}

bool stop_requested(const FullLoadContext* context)
{
    return (xEventGroupGetBits(context->events) & kStopBit) != 0U;
}

void display_task(void* argument)
{
    auto* context = static_cast<FullLoadContext*>(argument);
    const EventBits_t gate =
        xEventGroupWaitBits(context->events, kStartBit | kStopBit, pdFALSE, pdFALSE, portMAX_DELAY);
    if ((gate & kStopBit) == 0U) {
        while (!stop_requested(context)) {
            const auto result = context->display->draw_full_load_step();
            if (!result.ok()) {
                record_failure(context, "display_runtime", result.error, result.raw_error);
                break;
            }
        }
    }
    xEventGroupSetBits(context->events, kDisplayDoneBit);
    vTaskDelete(nullptr);
}

void audio_task(void* argument)
{
    auto* context = static_cast<FullLoadContext*>(argument);
    const EventBits_t gate =
        xEventGroupWaitBits(context->events, kStartBit | kStopBit, pdFALSE, pdFALSE, portMAX_DELAY);
    if ((gate & kStopBit) == 0U) {
        const auto result = context->audio->play_test_tone(kHardTimeoutMs, kHardTimeoutMs);
        if (!result.ok()) {
            record_failure(context, "audio_runtime", result.error, result.raw_error);
        }
    }
    xEventGroupSetBits(context->events, kAudioDoneBit);
    vTaskDelete(nullptr);
}

void configure_right_button()
{
    gpio_config_t config = {};
    config.pin_bit_mask  = 1ULL << m5sw::board::kKey2;
    config.mode          = GPIO_MODE_INPUT;
    config.pull_up_en    = GPIO_PULLUP_ENABLE;
    config.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    config.intr_type     = GPIO_INTR_DISABLE;
    require("right_button", gpio_config(&config));
}

i2c_bus_device_handle_t initialize_touch(i2c_bus_handle_t bus, m5sw::power::Ioe1Adapter* ioe1)
{
    auto result = ioe1->set_pin(m5sw::board::IoePin::touch_reset, false);
    if (!result.ok()) fail("touch_reset_low", result.error, result.raw_error);
    vTaskDelay(pdMS_TO_TICKS(kTouchResetLowMs));
    result = ioe1->set_pin(m5sw::board::IoePin::touch_reset, true);
    if (!result.ok()) fail("touch_reset_high", result.error, result.raw_error);
    vTaskDelay(pdMS_TO_TICKS(kTouchResetReadyMs));

    gpio_config_t irq_config = {};
    irq_config.pin_bit_mask  = 1ULL << m5sw::board::kTouchIrq;
    irq_config.mode          = GPIO_MODE_INPUT;
    irq_config.pull_up_en    = GPIO_PULLUP_DISABLE;
    irq_config.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    irq_config.intr_type     = GPIO_INTR_DISABLE;
    require("touch_irq", gpio_config(&irq_config));

    i2c_bus_device_handle_t device = i2c_bus_device_create(bus, m5sw::board::kTouchAddress, 100000);
    if (device == nullptr) fail("touch_device", ESP_ERR_NO_MEM, ESP_ERR_NO_MEM);

    std::uint8_t chip_id  = 0;
    std::uint8_t firmware = 0;
    esp_err_t error       = i2c_bus_read_byte(device, kTouchChipIdRegister, &chip_id);
    if (error != ESP_OK) fail("touch_chip_id", error, error);
    error = i2c_bus_read_byte(device, kTouchFirmwareRegister, &firmware);
    if (error != ESP_OK) fail("touch_firmware", error, error);
    if (chip_id == 0x00U || chip_id == 0xFFU || firmware == 0xFFU) {
        fail("touch_identity", ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_RESPONSE);
    }
    return device;
}

void baseline_ioe1(m5sw::power::Ioe1Adapter* ioe1)
{
    auto result = ioe1->stop_all_pwm();
    if (!result.ok()) fail("pwm_off", result.error, result.raw_error);
    constexpr m5sw::board::IoePin off_pins[] = {
        m5sw::board::IoePin::motor_pwm,   m5sw::board::IoePin::speaker_enable, m5sw::board::IoePin::audio_enable,
        m5sw::board::IoePin::touch_reset, m5sw::board::IoePin::mux_select,     m5sw::board::IoePin::display_reset,
        m5sw::board::IoePin::l3b_enable,
    };
    for (const auto pin : off_pins) {
        result = ioe1->set_pin(pin, false);
        if (!result.ok()) fail("ioe1_baseline", result.error, result.raw_error);
    }
}

FailureRecord shutdown_loads(m5sw::power::Ioe1Adapter* ioe1, m5sw::audio::AudioController* audio,
                             m5sw::display::DisplayController* display, m5sw::wifi::WifiController* wifi)
{
    FailureRecord failure = {};
    const auto capture    = [&failure](const char* stage, esp_err_t error, int raw_error) {
        if (error != ESP_OK && failure.stage == nullptr) {
            failure = {stage, error, raw_error};
        }
    };

    if (audio != nullptr) audio->request_stop();
    if (ioe1 != nullptr && ioe1->initialized()) {
        auto result = ioe1->set_motor_percent(0);
        capture("motor_stop", result.error, result.raw_error);
        result = ioe1->set_pin(m5sw::board::IoePin::speaker_enable, false);
        capture("speaker_stop", result.error, result.raw_error);
    }
    if (audio != nullptr && audio->initialized()) {
        const auto result = audio->stop();
        capture("audio_stop", result.error, result.raw_error);
    }
    if (display != nullptr && display->initialized()) {
        const auto result = display->set_brightness_percent(0);
        capture("display_dim", result.error, result.raw_error);
    }
    if (ioe1 != nullptr && ioe1->initialized()) {
        auto result = ioe1->stop_all_pwm();
        capture("pwm_stop", result.error, result.raw_error);
        result = ioe1->set_pin(m5sw::board::IoePin::audio_enable, false);
        capture("audio_rail_stop", result.error, result.raw_error);
        result = ioe1->set_pin(m5sw::board::IoePin::display_reset, false);
        capture("display_reset_stop", result.error, result.raw_error);
        result = ioe1->set_pin(m5sw::board::IoePin::l3b_enable, false);
        capture("display_rail_stop", result.error, result.raw_error);
    }
    if (wifi != nullptr && wifi->initialized()) {
        const auto result = wifi->deinitialize();
        capture("wifi_stop", result.error, result.raw_error);
    }
    return failure;
}

}  // namespace

extern "C" void app_main(void)
{
    m5sw::test_support::TestContext test_context = {};
    require("test_support", m5sw::test_support::initialize(&test_context));
#if !defined(CONFIG_M5SW_FULL_LOAD_SAFETY_ACK) || !CONFIG_M5SW_FULL_LOAD_SAFETY_ACK
    fail("safety_ack", ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_STATE);
#endif

    esp_log_level_set("i2c_bus", ESP_LOG_ERROR);
    require("i2c", m5sw::board::initialize_i2c(m5sw::board::I2cSpeed::standard));
    i2c_bus_handle_t bus = nullptr;
    require("i2c_claim", m5sw::board::acquire_i2c(&bus));

    constexpr m5sw::board::DeviceMask required_devices =
        m5sw::board::device_mask(m5sw::board::Device::pm1) | m5sw::board::device_mask(m5sw::board::Device::ioe1);
    m5sw::board::ProbeReport probe = {};
    require("board_probe", m5sw::board::probe_devices(&probe, required_devices));

    static m5sw::power::Pm1Adapter pm1;
    auto pm1_result = pm1.begin(bus, kReferenceI2cSpeedHz);
    if (!pm1_result.ok()) fail("pm1_begin", pm1_result.error, pm1_result.raw_error);
    pm1_result = pm1.set_external_5v_output(false);
    if (!pm1_result.ok()) fail("external_5v_off", pm1_result.error, pm1_result.raw_error);
    pm1_result = pm1.set_status_led(false);
    if (!pm1_result.ok()) fail("status_led_off", pm1_result.error, pm1_result.raw_error);

    static m5sw::power::Ioe1Adapter ioe1;
    auto ioe1_result = ioe1.begin(bus, kReferenceI2cSpeedHz);
    if (!ioe1_result.ok()) fail("ioe1_begin", ioe1_result.error, ioe1_result.raw_error);
    ioe1_result = ioe1.configure_i2c(false, kReferenceI2cSpeedHz);
    if (!ioe1_result.ok()) fail("ioe1_i2c", ioe1_result.error, ioe1_result.raw_error);
    baseline_ioe1(&ioe1);
    ioe1_result = ioe1.set_pwm_frequency(kMotorPwmFrequencyHz);
    if (!ioe1_result.ok()) fail("motor_frequency", ioe1_result.error, ioe1_result.raw_error);
    pm1_result =
        pm1.set_power_config(M5PM1_PWR_CFG_CHG_EN | M5PM1_PWR_CFG_LDO_EN, M5PM1_PWR_CFG_CHG_EN | M5PM1_PWR_CFG_LDO_EN);
    if (!pm1_result.ok()) fail("reference_power", pm1_result.error, pm1_result.raw_error);
    pm1_result = pm1.set_ldo_hold(true);
    if (!pm1_result.ok()) fail("l1_hold", pm1_result.error, pm1_result.raw_error);
    configure_right_button();

    static m5sw::display::DisplayController display;
    auto display_result = display.begin(bus, kReferenceI2cSpeedHz);
    if (!display_result.ok()) {
        shutdown_loads(&ioe1, nullptr, &display, nullptr);
        fail("display_begin", display_result.error, display_result.raw_error);
    }
    static m5sw::audio::AudioController audio;
    auto audio_result = audio.begin(bus, kReferenceI2cSpeedHz);
    if (!audio_result.ok()) {
        shutdown_loads(&ioe1, &audio, &display, nullptr);
        fail("audio_preinit", audio_result.error, audio_result.raw_error);
    }

    static i2c_bus_device_handle_t touch = initialize_touch(bus, &ioe1);
    (void)touch;

    display_result = display.show_white_static();
    if (!display_result.ok()) {
        shutdown_loads(&ioe1, &audio, &display, nullptr);
        fail("display_frame", display_result.error, display_result.raw_error);
    }
    ioe1_result = ioe1.set_motor_percent(100);
    if (!ioe1_result.ok()) {
        shutdown_loads(&ioe1, &audio, &display, nullptr);
        fail("motor_full", ioe1_result.error, ioe1_result.raw_error);
    }

    static m5sw::wifi::WifiController wifi;
    auto wifi_result = wifi.begin(m5sw::wifi::PowerSave::min_modem);
    if (!wifi_result.ok()) {
        shutdown_loads(&ioe1, &audio, &display, &wifi);
        fail("wifi_begin", wifi_result.error, wifi_result.raw_error);
    }

    audio_result = audio.begin(bus, kReferenceI2cSpeedHz);
    if (!audio_result.ok()) {
        shutdown_loads(&ioe1, &audio, &display, &wifi);
        fail("audio_begin", audio_result.error, audio_result.raw_error);
    }

    static FullLoadContext load_context = {};
    load_context.events                 = xEventGroupCreate();
    load_context.display                = &display;
    load_context.wifi                   = &wifi;
    load_context.audio                  = &audio;
    if (load_context.events == nullptr) {
        shutdown_loads(&ioe1, &audio, &display, &wifi);
        fail("event_group", ESP_ERR_NO_MEM, ESP_ERR_NO_MEM);
    }

    const BaseType_t display_created =
        xTaskCreate(display_task, "full_display", kDisplayTaskStackBytes, &load_context, 5, nullptr);
    const BaseType_t audio_created =
        xTaskCreate(audio_task, "full_audio", kAudioTaskStackBytes, &load_context, 6, nullptr);
    if (display_created != pdPASS || audio_created != pdPASS) {
        xEventGroupSetBits(load_context.events, kStartBit | kStopBit);
        vTaskDelay(pdMS_TO_TICKS(20));
        shutdown_loads(&ioe1, &audio, &display, &wifi);
        fail("task_create", ESP_ERR_NO_MEM, ESP_ERR_NO_MEM);
    }

    ESP_LOGI(kTag,
             "BOARD_OK PM1=1 IOE1=1 TP=1 L1=on PROFILE=reference profile_id=full-load "
             "DISPLAY_PERCENT=50 DISPLAY_RAW=128 DISPLAY_LINES=466 FRAMEBUFFER=auto "
             "DISPLAY_MODE=random_unthrottled "
             "WIFI=initialized WIFI_PS=min_modem WIFI_SCAN=off "
             "AUDIO_HZ=44100 AUDIO_BITS=16 AUDIO_CHANNELS=2 AUDIO_IO=tx_rx "
             "CODEC_MODE=in_out AUDIO_INPUT_GAIN_DB=30 "
             "AUDIO_VOLUME_PERCENT=100 MOTOR_PERCENT=100 MOTOR_HZ=5000 "
             "I2C_HZ=400000 AUDIO_RMS_PERCENT=54 "
             "STOP=right_key HARD_TIMEOUT_MS=600000 EXT5V=0");
    const esp_err_t ready_result = m5sw::test_support::mark_test_ready();
    if (ready_result != ESP_OK) {
        xEventGroupSetBits(load_context.events, kStartBit | kStopBit);
        audio.request_stop();
        vTaskDelay(pdMS_TO_TICKS(20));
        (void)shutdown_loads(&ioe1, &audio, &display, &wifi);
        fail("test_ready", ready_result, ready_result);
    }

    const TickType_t load_start = xTaskGetTickCount();
    xEventGroupSetBits(load_context.events, kStartBit);

    bool stopped_by_right_key = false;
    int previous_right_level  = gpio_get_level(m5sw::board::kKey2);
    while ((xEventGroupGetBits(load_context.events) & kFailureBit) == 0U) {
        const int right_level = gpio_get_level(m5sw::board::kKey2);
        if (previous_right_level == 1 && right_level == 0) {
            stopped_by_right_key = true;
            break;
        }
        previous_right_level = right_level;
        if ((xTaskGetTickCount() - load_start) >= pdMS_TO_TICKS(kHardTimeoutMs)) {
            record_failure(&load_context, "hard_timeout", ESP_ERR_TIMEOUT, ESP_ERR_TIMEOUT);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    xEventGroupSetBits(load_context.events, kStopBit);
    audio.request_stop();
    ioe1_result = ioe1.set_motor_percent(0);
    if (!ioe1_result.ok()) {
        record_failure(&load_context, "motor_stop", ioe1_result.error, ioe1_result.raw_error);
    }

    const EventBits_t done =
        xEventGroupWaitBits(load_context.events, kAllDoneBits, pdFALSE, pdTRUE, pdMS_TO_TICKS(kShutdownTimeoutMs));
    if ((done & kAllDoneBits) != kAllDoneBits) {
        record_failure(&load_context, "shutdown_timeout", ESP_ERR_TIMEOUT, ESP_ERR_TIMEOUT);
    }

    const FailureRecord shutdown_failure = shutdown_loads(&ioe1, &audio, &display, &wifi);
    if (shutdown_failure.stage != nullptr) {
        record_failure(&load_context, shutdown_failure.stage, shutdown_failure.error, shutdown_failure.raw_error);
    }
    const FailureRecord final_failure = failure_snapshot();
    if (final_failure.stage != nullptr) {
        ESP_LOGE(kTag, "FULL_LOAD_FAIL stage=%s error=%s raw=%d",
                 final_failure.stage == nullptr ? "runtime" : final_failure.stage, esp_err_to_name(final_failure.error),
                 final_failure.raw_error);
        m5sw::test_support::report_final(m5sw::test_support::FinalStatus::fail, "Full Load stopped and converged safe");
    } else {
        m5sw::test_support::report_final(
            m5sw::test_support::FinalStatus::pass,
            stopped_by_right_key ? "Full Load stopped by right key and converged safe" : "Full Load converged safe");
    }
    m5sw::test_support::hold_safe_idle();
}
