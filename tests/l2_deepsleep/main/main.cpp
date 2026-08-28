#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "m5sw_board.hpp"
#include "m5sw_power.hpp"
#include "m5sw_test_support.hpp"

namespace {
constexpr char kTag[] = "l2_deepsleep";
constexpr TickType_t kPrepareDelay = pdMS_TO_TICKS(3000);

[[noreturn]] void fail(const char* stage, esp_err_t error, int raw_error)
{
    ESP_LOGE(kTag, "BOARD_FAIL stage=%s error=%s raw=%d", stage,
             esp_err_to_name(error), raw_error);
    m5sw::test_support::report_final(m5sw::test_support::FinalStatus::fail,
                                     "L2 preparation failed");
    m5sw::test_support::hold_safe_idle();
}
}  // namespace

extern "C" void app_main(void)
{
    m5sw::test_support::TestContext context = {};
    ESP_ERROR_CHECK(m5sw::test_support::initialize(&context));
    esp_log_level_set("i2c_bus", ESP_LOG_ERROR);

    const esp_err_t i2c_result = m5sw::board::initialize_i2c();
    if (i2c_result != ESP_OK) fail("i2c", i2c_result, i2c_result);

    i2c_bus_handle_t bus = nullptr;
    const esp_err_t claim_result = m5sw::board::acquire_i2c(&bus);
    if (claim_result != ESP_OK) fail("shared_bus", claim_result, claim_result);

    static m5sw::power::PowerController power;
    auto result = power.begin(bus);
    if (!result.ok()) fail("pm1_begin", result.error, result.raw_error);

    // Finish all peripheral shutdown and wake isolation before TEST_READY.
    result = power.prepare_l2(m5sw::power::WakeSelection::none);
    if (!result.ok()) fail("l2_prepare", result.error, result.raw_error);

    ESP_LOGI(kTag, "BOARD_OK PM1=1 IOE1=1 WAKE=none");
    ESP_ERROR_CHECK(m5sw::test_support::mark_test_ready());
    vTaskDelay(kPrepareDelay);

    result = power.enter_l2(m5sw::power::WakeSelection::none);
    fail("l2_returned", result.error, result.raw_error);
}
