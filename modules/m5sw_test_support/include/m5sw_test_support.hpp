#pragma once

#include <cstdint>

#include "esp_err.h"
#include "esp_sleep.h"
#include "esp_system.h"

namespace m5sw::test_support {

enum class FinalStatus : std::uint8_t {
    pass,
    fail,
};

struct TestContext {
    const char* test_id = nullptr;
    const char* firmware_version = nullptr;
    std::uint32_t config_hash = 0;
    esp_reset_reason_t reset_reason = ESP_RST_UNKNOWN;
    esp_sleep_wakeup_cause_t wake_cause = ESP_SLEEP_WAKEUP_UNDEFINED;
};

// Captures reset/wake metadata and emits the single startup identification record.
esp_err_t initialize(TestContext* context);

// Emits TEST_READY once after the caller has completed target configuration.
// The call flushes buffered serial output before a low-power transition or measurement.
esp_err_t mark_test_ready();

// Periodic diagnostics are allowed only before TEST_READY.
[[nodiscard]] bool periodic_logs_enabled();
// Returns whether diagnostic polling is still allowed by the test lifecycle.
[[nodiscard]] bool diagnostics_enabled();
// Returns whether the one-shot TEST_READY transition has completed.
[[nodiscard]] bool test_ready();

// Emits one final PASS/FAIL record; no periodic logging is started by this function.
void report_final(FinalStatus status, const char* detail = nullptr);
// Blocks forever without touching hardware after a fatal test setup failure.
void hold_safe_idle();

// Maps ESP reset causes to stable log labels used by TEST_START.
[[nodiscard]] const char* reset_reason_name(esp_reset_reason_t reason);
// Maps ESP wake causes to stable log labels used by wake evidence.
[[nodiscard]] const char* wake_cause_name(esp_sleep_wakeup_cause_t cause);

}  // namespace m5sw::test_support
