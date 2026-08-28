#pragma once

#include <cstdint>

#include "esp_err.h"

namespace m5sw::wifi {

inline constexpr std::uint32_t kPeriodicTrafficIntervalMs   = 1000;
inline constexpr std::uint32_t kPeriodicTrafficPayloadBytes = 64;

enum class PowerSave : std::uint8_t {
    none,
    min_modem,
    max_modem,
};

enum class State : std::uint8_t {
    stopped,
    disconnected,
    connecting,
    connected,
};

struct WifiResult {
    esp_err_t error = ESP_OK;
    int raw_error   = 0;

    [[nodiscard]] bool ok() const
    {
        return error == ESP_OK;
    }
};

struct NetworkInfo {
    // Signal strength reported by the connected AP, in dBm.
    std::int8_t rssi_dbm = 0;
    // Primary channel reported by the connected AP.
    std::uint8_t channel = 0;
    // STA listen interval, expressed in AP beacon intervals.
    std::uint16_t listen_interval = 0;
    // Assigned IPv4 address in the native lwIP representation.
    std::uint32_t ipv4_address = 0;
};

struct TrafficStats {
    // Number of completed ICMP attempts, including replies and timeouts.
    std::uint32_t attempts = 0;
    // Number of attempts that received an ICMP reply.
    std::uint32_t replies = 0;
    // Number of attempts that reached the configured timeout.
    std::uint32_t timeouts = 0;
    // Sum of successful-reply round-trip times, in milliseconds.
    std::uint64_t total_rtt_ms = 0;
};

class WifiController final {
public:
    WifiController() = default;
    ~WifiController();
    WifiController(const WifiController&)            = delete;
    WifiController& operator=(const WifiController&) = delete;

    // Public operations are caller-serialized; do not invoke them concurrently.
    // Initializes shared TCP/IP infrastructure and an owned STA interface.
    // Existing NVS contents are never erased when initialization fails.
    [[nodiscard]] WifiResult begin(PowerSave initial_power_save = PowerSave::none);
    // Connects using only the local Kconfig credentials and enables reconnects.
    [[nodiscard]] WifiResult connect(std::uint32_t timeout_ms);
    // Stops automatic reconnect and disconnects the owned station interface.
    [[nodiscard]] WifiResult disconnect();
    // Applies and reads back the explicit Wi-Fi power-save mode.
    [[nodiscard]] WifiResult set_power_save(PowerSave mode);
    // Runs one blocking scan while connected and releases the driver's AP list.
    [[nodiscard]] WifiResult scan(std::uint16_t* access_point_count);
    // Sends one 64-byte ICMP Echo to the configured IPv4 target without logging it.
    [[nodiscard]] WifiResult ping_once(std::uint32_t timeout_ms);
    // Reads the current AP RSSI/channel, STA listen interval, and IPv4 address.
    [[nodiscard]] WifiResult network_info(NetworkInfo* info) const;
    // Stops and releases resources owned by this controller; shared event/NVS state remains.
    [[nodiscard]] WifiResult deinitialize();

    // Reports whether the STA controller owns an initialized Wi-Fi stack.
    [[nodiscard]] bool initialized() const;
    // Reports whether non-empty build-time SSID credentials are available.
    [[nodiscard]] bool credentials_configured() const;
    // Reports whether a valid target IPv4 string is available for Ping traffic.
    [[nodiscard]] bool target_configured() const;
    // Returns the controller lifecycle state maintained by Wi-Fi event callbacks.
    [[nodiscard]] State state() const;
    // Returns the power-save mode last verified against the Wi-Fi driver.
    [[nodiscard]] PowerSave power_save() const;
    // Returns accumulated Ping attempt, reply, timeout and RTT statistics.
    [[nodiscard]] TrafficStats traffic_stats() const;

private:
    class Impl;
    Impl* impl_ = nullptr;
};

}  // namespace m5sw::wifi
