#include "m5sw_audio.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_types.h"
#include "es8311_codec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "m5sw_board.hpp"
#include "m5sw_ioe1.hpp"

namespace m5sw::audio {
namespace {

constexpr i2s_port_t kI2sPort         = I2S_NUM_0;
constexpr std::size_t kFramesPerChunk = 441;
// Matches the measured RMS of the validated reference tone.wav without copying it.
constexpr std::int16_t kToneAmplitude      = 30536;
constexpr std::uint32_t kAudioRailSettleMs = 10;

static_assert(ES8311_CODEC_DEFAULT_ADDR == (m5sw::board::kAudioCodecAddress << 1));

AudioResult from_codec(int raw)
{
    return {raw == ESP_CODEC_DEV_OK ? ESP_OK : static_cast<esp_err_t>(raw), raw};
}

AudioResult from_ioe1(m5sw::power::Ioe1Result result)
{
    return {result.error, result.raw_error};
}

}  // namespace

class AudioController::Impl final {
public:
    void fill_tone()
    {
        // A project-authored approximately 1 kHz stereo triangle wave.
        for (std::size_t frame = 0; frame < kFramesPerChunk; ++frame) {
            const std::uint32_t phase = static_cast<std::uint32_t>(frame % 44U);
            const std::int32_t ramp =
                phase < 22U ? static_cast<std::int32_t>(phase) : static_cast<std::int32_t>(44U - phase);
            const std::int16_t sample = static_cast<std::int16_t>(((ramp * 2 - 22) * kToneAmplitude) / 22);
            for (std::size_t channel = 0; channel < kChannelCount; ++channel) {
                tone[frame * kChannelCount + channel] = sample;
            }
        }
    }

    AudioResult set_speaker(bool enabled)
    {
        return from_ioe1(ioe1.set_pin(m5sw::board::IoePin::speaker_enable, enabled));
    }

    void release()
    {
        if (ioe1.initialized()) (void)set_speaker(false);
        if (codec_device != nullptr) {
            (void)esp_codec_dev_set_out_mute(codec_device, true);
            if (codec_open) (void)esp_codec_dev_close(codec_device);
            esp_codec_dev_delete(codec_device);
            codec_device = nullptr;
            codec_open   = false;
        }
        if (codec_if != nullptr) {
            (void)audio_codec_delete_codec_if(codec_if);
            codec_if = nullptr;
        }
        if (ctrl_if != nullptr) {
            (void)audio_codec_delete_ctrl_if(ctrl_if);
            ctrl_if = nullptr;
        }
        if (gpio_if != nullptr) {
            (void)audio_codec_delete_gpio_if(gpio_if);
            gpio_if = nullptr;
        }
        if (data_if != nullptr) {
            (void)audio_codec_delete_data_if(data_if);
            data_if = nullptr;
        }
        if (tx_channel != nullptr) {
            (void)i2s_channel_disable(tx_channel);
            (void)i2s_del_channel(tx_channel);
            tx_channel = nullptr;
        }
        if (rx_channel != nullptr) {
            (void)i2s_channel_disable(rx_channel);
            (void)i2s_del_channel(rx_channel);
            rx_channel = nullptr;
        }
        if (ioe1.initialized()) {
            (void)ioe1.set_pin(m5sw::board::IoePin::audio_enable, false);
        }
    }

    m5sw::power::Ioe1Adapter ioe1;
    i2s_chan_handle_t tx_channel                                   = nullptr;
    i2s_chan_handle_t rx_channel                                   = nullptr;
    esp_codec_dev_handle_t codec_device                            = nullptr;
    const audio_codec_data_if_t* data_if                           = nullptr;
    const audio_codec_ctrl_if_t* ctrl_if                           = nullptr;
    const audio_codec_gpio_if_t* gpio_if                           = nullptr;
    const audio_codec_if_t* codec_if                               = nullptr;
    bool codec_open                                                = false;
    std::array<std::int16_t, kFramesPerChunk * kChannelCount> tone = {};
    std::atomic_bool stop_requested{false};
};

AudioController::~AudioController()
{
    if (impl_ != nullptr) {
        impl_->release();
        delete impl_;
    }
}

AudioResult AudioController::begin(i2c_bus_handle_t bus, std::uint32_t i2c_speed_hz)
{
    if (bus == nullptr || (i2c_speed_hz != 100000U && i2c_speed_hz != 400000U)) {
        return {ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_ARG};
    }
    if (initialized_) return {};

    if (impl_ == nullptr) {
        impl_ = new (std::nothrow) Impl();
        if (impl_ == nullptr) return {ESP_ERR_NO_MEM, ESP_ERR_NO_MEM};
    }

    const auto fail = [this](AudioResult error) {
        impl_->release();
        initialized_ = false;
        playing_     = false;
        return error;
    };

    auto ioe_result = impl_->ioe1.begin(bus, i2c_speed_hz);
    if (!ioe_result.ok()) return fail(from_ioe1(ioe_result));
    ioe_result = impl_->ioe1.configure_i2c(false, i2c_speed_hz);
    if (!ioe_result.ok()) return fail(from_ioe1(ioe_result));
    ioe_result = impl_->ioe1.set_pin(m5sw::board::IoePin::speaker_enable, false);
    if (!ioe_result.ok()) return fail(from_ioe1(ioe_result));
    ioe_result = impl_->ioe1.set_pin(m5sw::board::IoePin::audio_enable, true);
    if (!ioe_result.ok()) return fail(from_ioe1(ioe_result));
    vTaskDelay(pdMS_TO_TICKS(kAudioRailSettleMs));

    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(kI2sPort, I2S_ROLE_MASTER);
    esp_err_t result                 = i2s_new_channel(&channel_config, &impl_->tx_channel, &impl_->rx_channel);
    if (result != ESP_OK) return fail({result, result});

    i2s_std_config_t standard_config      = {};
    standard_config.clk_cfg               = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRateHz);
    standard_config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    standard_config.slot_cfg      = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    standard_config.gpio_cfg.mclk = m5sw::board::kI2sMclk;
    standard_config.gpio_cfg.bclk = m5sw::board::kI2sBclk;
    standard_config.gpio_cfg.ws   = m5sw::board::kI2sLrck;
    standard_config.gpio_cfg.dout = m5sw::board::kI2sDacData;
    standard_config.gpio_cfg.din  = m5sw::board::kI2sAdcData;
    standard_config.gpio_cfg.invert_flags = {};
    result                                = i2s_channel_init_std_mode(impl_->tx_channel, &standard_config);
    if (result != ESP_OK) return fail({result, result});
    result = i2s_channel_init_std_mode(impl_->rx_channel, &standard_config);
    if (result != ESP_OK) return fail({result, result});
    result = i2s_channel_enable(impl_->tx_channel);
    if (result != ESP_OK) return fail({result, result});
    result = i2s_channel_enable(impl_->rx_channel);
    if (result != ESP_OK) return fail({result, result});

    audio_codec_i2s_cfg_t i2s_config = {};
    i2s_config.port                  = static_cast<std::uint8_t>(kI2sPort);
    i2s_config.tx_handle             = impl_->tx_channel;
    i2s_config.rx_handle             = impl_->rx_channel;
    impl_->data_if                   = audio_codec_new_i2s_data(&i2s_config);
    if (impl_->data_if == nullptr) return fail({ESP_ERR_NO_MEM, ESP_ERR_NO_MEM});

    audio_codec_i2c_cfg_t i2c_config = {};
    i2c_config.addr                  = ES8311_CODEC_DEFAULT_ADDR;
    i2c_config.bus_handle            = i2c_bus_get_internal_bus_handle(bus);
    i2c_config.clock_speed_hz        = static_cast<int>(i2c_speed_hz);
    impl_->ctrl_if                   = audio_codec_new_i2c_ctrl(&i2c_config);
    if (impl_->ctrl_if == nullptr) return fail({ESP_FAIL, ESP_FAIL});

    impl_->gpio_if = audio_codec_new_gpio();
    if (impl_->gpio_if == nullptr) return fail({ESP_ERR_NO_MEM, ESP_ERR_NO_MEM});

    es8311_codec_cfg_t codec_config = {};
    codec_config.ctrl_if            = impl_->ctrl_if;
    codec_config.gpio_if            = impl_->gpio_if;
    codec_config.codec_mode         = ESP_CODEC_DEV_WORK_MODE_BOTH;
    codec_config.pa_pin             = GPIO_NUM_NC;
    codec_config.pa_reverted        = false;
    codec_config.master_mode        = false;
    codec_config.use_mclk           = true;
    codec_config.mclk_div           = 256;
    impl_->codec_if                 = es8311_codec_new(&codec_config);
    if (impl_->codec_if == nullptr) return fail({ESP_FAIL, ESP_FAIL});

    esp_codec_dev_cfg_t device_config = {};
    device_config.dev_type            = ESP_CODEC_DEV_TYPE_IN_OUT;
    device_config.codec_if            = impl_->codec_if;
    device_config.data_if             = impl_->data_if;
    impl_->codec_device               = esp_codec_dev_new(&device_config);
    if (impl_->codec_device == nullptr) return fail({ESP_ERR_NO_MEM, ESP_ERR_NO_MEM});

    esp_codec_dev_sample_info_t sample_info = {};
    sample_info.bits_per_sample             = kBitsPerSample;
    sample_info.channel                     = kChannelCount;
    sample_info.sample_rate                 = kSampleRateHz;
    sample_info.mclk_multiple               = 256;
    int codec_result                        = esp_codec_dev_open(impl_->codec_device, &sample_info);
    if (codec_result != ESP_CODEC_DEV_OK) return fail(from_codec(codec_result));
    impl_->codec_open = true;
    codec_result      = esp_codec_dev_set_out_vol(impl_->codec_device, kOutputVolumePercent);
    if (codec_result != ESP_CODEC_DEV_OK) return fail(from_codec(codec_result));
    codec_result = esp_codec_dev_set_in_gain(impl_->codec_device, kInputGainDb);
    if (codec_result != ESP_CODEC_DEV_OK) return fail(from_codec(codec_result));
    codec_result = esp_codec_dev_set_out_mute(impl_->codec_device, true);
    if (codec_result != ESP_CODEC_DEV_OK) return fail(from_codec(codec_result));

    impl_->fill_tone();
    initialized_ = true;
    return {};
}

AudioResult AudioController::play_test_tone(std::uint32_t duration_ms, std::uint32_t hard_timeout_ms)
{
    if (!initialized_) return {ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_STATE};
    if (duration_ms == 0U || hard_timeout_ms == 0U) {
        return {ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_ARG};
    }
    if (playing_) return {ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_STATE};
    if (impl_->stop_requested.load()) return stop();

    int codec_result = esp_codec_dev_set_out_mute(impl_->codec_device, false);
    if (codec_result != ESP_CODEC_DEV_OK) {
        (void)impl_->set_speaker(false);
        return from_codec(codec_result);
    }
    AudioResult speaker_result = impl_->set_speaker(true);
    if (!speaker_result.ok()) {
        (void)esp_codec_dev_set_out_mute(impl_->codec_device, true);
        return speaker_result;
    }

    playing_                        = true;
    const TickType_t start          = xTaskGetTickCount();
    const TickType_t duration_ticks = pdMS_TO_TICKS(duration_ms);
    const TickType_t timeout_ticks  = pdMS_TO_TICKS(hard_timeout_ms);
    AudioResult playback_result     = {};
    while ((xTaskGetTickCount() - start) < duration_ticks) {
        if (impl_->stop_requested.load()) break;
        if ((xTaskGetTickCount() - start) >= timeout_ticks) {
            playback_result = {ESP_ERR_TIMEOUT, ESP_ERR_TIMEOUT};
            break;
        }
        codec_result = esp_codec_dev_write(impl_->codec_device, impl_->tone.data(),
                                           static_cast<int>(impl_->tone.size() * sizeof(impl_->tone[0])));
        if (codec_result != ESP_CODEC_DEV_OK) {
            playback_result = from_codec(codec_result);
            break;
        }
    }

    const AudioResult stop_result = stop();
    return playback_result.ok() ? stop_result : playback_result;
}

void AudioController::request_stop()
{
    if (impl_ != nullptr) impl_->stop_requested.store(true);
}

AudioResult AudioController::stop()
{
    if (!initialized_) return {ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_STATE};
    const AudioResult speaker_result = impl_->set_speaker(false);
    const int mute_result            = esp_codec_dev_set_out_mute(impl_->codec_device, true);
    playing_                         = false;
    impl_->stop_requested.store(false);
    if (!speaker_result.ok()) return speaker_result;
    return from_codec(mute_result);
}

}  // namespace m5sw::audio
