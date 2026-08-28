#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "m5sw_board.hpp"
#include "m5sw_power.hpp"
#include "m5sw_test_support.hpp"

namespace {
constexpr char kTag[]              = "l0_shutdown";
constexpr TickType_t kPrepareDelay = pdMS_TO_TICKS(3000);
}  // namespace

extern "C" void app_main(void)
{
    m5sw::test_support::TestContext context = {};
    ESP_ERROR_CHECK(m5sw::test_support::initialize(&context));

    // L0 is a PM1-only path. It must not probe or initialize IOE1, RTC, or IMU.
    esp_log_level_set("i2c_bus", ESP_LOG_ERROR);
    const esp_err_t i2c_result = m5sw::board::initialize_i2c();
    if (i2c_result != ESP_OK) {
        ESP_LOGE(kTag, "BOARD_FAIL stage=i2c error=%s", esp_err_to_name(i2c_result));
        m5sw::test_support::report_final(m5sw::test_support::FinalStatus::fail, "I2C initialization failed");
        m5sw::test_support::hold_safe_idle();
    }

    i2c_bus_handle_t bus         = nullptr;
    const esp_err_t claim_result = m5sw::board::acquire_i2c(&bus);
    if (claim_result != ESP_OK) {
        ESP_LOGE(kTag, "BOARD_FAIL stage=pm1_bus error=%s", esp_err_to_name(claim_result));
        m5sw::test_support::report_final(m5sw::test_support::FinalStatus::fail, "PM1 shared I2C claim failed");
        m5sw::test_support::hold_safe_idle();
    }

    m5sw::power::PowerController power;
    const m5sw::power::PowerResult begin_result = power.begin(bus);
    if (!begin_result.ok()) {
        ESP_LOGE(kTag, "BOARD_FAIL stage=pm1_begin error=%s raw=%d", esp_err_to_name(begin_result.error),
                 begin_result.raw_error);
        m5sw::test_support::report_final(m5sw::test_support::FinalStatus::fail, "PM1 initialization failed");
        m5sw::test_support::hold_safe_idle();
    }
    ESP_LOGI(kTag, "BOARD_OK PM1=1");

    ESP_ERROR_CHECK(m5sw::test_support::mark_test_ready());
    vTaskDelay(kPrepareDelay);

    const m5sw::power::PowerResult l0_result = power.enter_l0();
    if (!l0_result.ok()) {
        ESP_LOGE(kTag, "TEST_FAIL stage=l0_enter error=%s raw=%d", esp_err_to_name(l0_result.error),
                 l0_result.raw_error);
        m5sw::test_support::report_final(m5sw::test_support::FinalStatus::fail, "L0 shutdown failed");
        m5sw::test_support::hold_safe_idle();
    }

    // PM1 shutdown is expected to remove the remaining L0 power path. No further
    // logging or peripheral access is allowed after this point.
    m5sw::test_support::hold_safe_idle();
}
