#pragma once

#include <cstdint>

#include "esp_err.h"
#include "i2c_bus.h"

namespace m5sw::audio {

inline constexpr std::uint32_t kSampleRateHz       = 44100;
inline constexpr std::uint8_t kBitsPerSample       = 16;
inline constexpr std::uint8_t kChannelCount        = 2;
inline constexpr std::uint8_t kOutputVolumePercent = 100;
inline constexpr float kInputGainDb                = 30.0F;

struct AudioResult {
    esp_err_t error = ESP_OK;
    int raw_error   = 0;

    [[nodiscard]] bool ok() const
    {
        return error == ESP_OK;
    }
};

class AudioController final {
public:
    AudioController() = default;
    ~AudioController();
    AudioController(const AudioController&)            = delete;
    AudioController& operator=(const AudioController&) = delete;

    // Borrows the shared I2C bus and initializes IOE1, ES8311 and I2S TX/RX.
    // The speaker amplifier remains disabled until playback starts.
    [[nodiscard]] AudioResult begin(i2c_bus_handle_t bus, std::uint32_t i2c_speed_hz = 100000);

    // Plays a project-authored deterministic tone for duration_ms. The hard
    // timeout is checked between bounded writes and always disables the PA.
    [[nodiscard]] AudioResult play_test_tone(std::uint32_t duration_ms, std::uint32_t hard_timeout_ms);

    // Requests cancellation without touching codec or I2C state. The playback
    // owner observes the request between bounded writes and performs shutdown.
    void request_stop();

    // Mutes output and disables the speaker amplifier. Repeated calls are safe.
    [[nodiscard]] AudioResult stop();

    // Reports whether codec, I2S and the audio power rail are initialized.
    [[nodiscard]] bool initialized() const
    {
        return initialized_;
    }
    // Reports whether a tone write loop currently owns active playback.
    [[nodiscard]] bool playing() const
    {
        return playing_;
    }

private:
    class Impl;
    Impl* impl_       = nullptr;
    bool initialized_ = false;
    bool playing_     = false;
};

}  // namespace m5sw::audio
