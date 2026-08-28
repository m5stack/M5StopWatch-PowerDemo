#include "m5sw_display.hpp"

#include <cstdio>
#include <cstdlib>
#include <new>

#include "M5GFX.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lgfx/v1/panel/Panel_CO5300.hpp"
#include "m5sw_board.hpp"
#include "m5sw_ioe1.hpp"

namespace m5sw::display {
namespace {

constexpr gpio_num_t kPanelTe         = GPIO_NUM_38;
constexpr gpio_num_t kPanelCs         = GPIO_NUM_39;
constexpr gpio_num_t kPanelClock      = GPIO_NUM_40;
constexpr gpio_num_t kPanelData0      = GPIO_NUM_41;
constexpr gpio_num_t kPanelData1      = GPIO_NUM_42;
constexpr gpio_num_t kPanelData2      = GPIO_NUM_46;
constexpr gpio_num_t kPanelData3      = GPIO_NUM_45;
constexpr std::uint32_t kPanelWriteHz = 80000000;
constexpr std::uint32_t kPanelReadHz  = 10000000;

class StopwatchPanel final : public lgfx::Panel_CO5300 {
public:
    StopwatchPanel()
    {
        _cfg.memory_width = _cfg.panel_width = 480;
        _cfg.memory_height = _cfg.panel_height = 480;
    }

    const std::uint8_t* getInitCommands(std::uint8_t list_number) const override
    {
        static constexpr std::uint8_t commands[] = {
            0x11, 0 + CMD_INIT_DELAY,
            150,  0xC4,
            1,    0x80,
            0x35, 1,
            0x80, 0x44,
            2,    0x01,
            0xD2, 0x53,
            1,    0x20,
            0x20, 0,
            0x36, 1,
            0,    0x51,
            1,    0xA0,
            0x29, 0,
            0xFF, 0xFF,
        };
        return list_number == 0 ? commands : nullptr;
    }
};

DisplayResult from_ioe1(m5sw::power::Ioe1Result result)
{
    return {result.error, result.raw_error};
}

}  // namespace

class DisplayController::Impl final : public lgfx::LGFX_Device {
public:
    Impl()
    {
        auto bus_config        = bus_.config();
        bus_config.freq_write  = kPanelWriteHz;
        bus_config.freq_read   = kPanelReadHz;
        bus_config.pin_sclk    = kPanelClock;
        bus_config.pin_mosi    = GPIO_NUM_NC;
        bus_config.pin_miso    = GPIO_NUM_NC;
        bus_config.pin_io0     = kPanelData0;
        bus_config.pin_io1     = kPanelData1;
        bus_config.pin_io2     = kPanelData2;
        bus_config.pin_io3     = kPanelData3;
        bus_config.spi_host    = SPI2_HOST;
        bus_config.spi_mode    = 0;
        bus_config.spi_3wire   = true;
        bus_config.dma_channel = SPI_DMA_CH_AUTO;
        bus_.config(bus_config);
        panel_.setBus(&bus_);

        auto panel_config            = panel_.config();
        panel_config.pin_cs          = kPanelCs;
        panel_config.pin_rst         = GPIO_NUM_NC;
        panel_config.pin_busy        = GPIO_NUM_NC;
        panel_config.panel_width     = 468;
        panel_config.panel_height    = 466;
        panel_config.offset_x        = 6;
        panel_config.offset_y        = 0;
        panel_config.offset_rotation = 0;
        panel_config.readable        = false;
        panel_config.invert          = false;
        panel_config.bus_shared      = false;
        panel_.config(panel_config);
        panel_.setRotation(0);
        setPanel(&panel_);
    }

    void draw_full_load_step()
    {
        ++full_load_frame_count_;
        const std::uint32_t second = static_cast<std::uint32_t>(esp_timer_get_time() / 1000000LL);
        if (full_load_previous_second_ != second) {
            full_load_previous_second_ = second;
            std::printf("count:%lu\n", static_cast<unsigned long>(full_load_frame_count_));
            std::fflush(stdout);
            vTaskDelay(pdMS_TO_TICKS(1));
            full_load_frame_count_ = 0;
            fillScreen(static_cast<std::uint32_t>(std::rand()));
            setRotation(getRotation() + 1);
            setCursor(112, 48);
            println("Stop Watch!");
        }

        fillCircle(105 + (std::rand() & 255), 105 + (std::rand() & 255), 16, static_cast<std::uint32_t>(std::rand()));
    }

    bool enable_framebuffer()
    {
        if (!panel_.initPanelFb()) return false;
        auto* framebuffer = panel_.getPanelFb();
        if (framebuffer == nullptr) return false;
        framebuffer->setBus(&bus_);
        framebuffer->setAutoDisplay(true);
        setPanel(framebuffer);
        return true;
    }

    void disable_framebuffer()
    {
        if (panel_.getPanelFb() != nullptr) {
            setPanel(&panel_);
            panel_.deinitPanelFb();
        }
    }

    void set_panel_brightness(std::uint8_t value)
    {
        panel_.setBrightness(value);
    }

    void draw_normal_frame(const char* time_text)
    {
        // Keep the framebuffer transaction open until the complete frame update
        // is ready so auto-display never exposes partially drawn content.
        startWrite();
        if (!normal_frame_initialized_) {
            fillScreen(TFT_BLACK);
            drawCircle(234, 234, 188, 0x4208U);
            drawCircle(234, 234, 176, 0x2104U);

            setFont(&fonts::Font0);
            setTextDatum(lgfx::textdatum_t::middle_center);
            setTextColor(TFT_WHITE, TFT_BLACK);
            setTextSize(2);
            drawString("STOPWATCH", 234, 154);
            setTextSize(1);
            setTextColor(0x07FFU, TFT_BLACK);
            drawString("ACTIVE", 234, 316);
            normal_frame_initialized_ = true;
        } else {
            fillRect(130, 204, 208, 60, TFT_BLACK);
        }

        setFont(&fonts::AsciiFont24x48);
        setTextDatum(lgfx::textdatum_t::middle_center);
        setTextColor(TFT_WHITE, TFT_BLACK);
        setTextSize(1);
        drawString(time_text, 234, 234);
        endWrite();
    }

    m5sw::power::Ioe1Adapter ioe1;

private:
    std::uint32_t full_load_previous_second_ = 0;
    std::uint32_t full_load_frame_count_     = 0;
    bool normal_frame_initialized_           = false;
    lgfx::Bus_SPI bus_;
    StopwatchPanel panel_;
};

DisplayController::~DisplayController()
{
    if (impl_ != nullptr) {
        if (initialized_) {
            impl_->disable_framebuffer();
            impl_->releaseBus();
        }
        delete impl_;
    }
}

DisplayResult DisplayController::begin(i2c_bus_handle_t bus, std::uint32_t i2c_speed_hz)
{
    if (bus == nullptr || (i2c_speed_hz != 100000U && i2c_speed_hz != 400000U)) {
        return {ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_ARG};
    }
    if (initialized_) return {};

    if (impl_ == nullptr) {
        impl_ = new (std::nothrow) Impl();
        if (impl_ == nullptr) return {ESP_ERR_NO_MEM, ESP_ERR_NO_MEM};
    }

    auto ioe_result = impl_->ioe1.begin(bus, i2c_speed_hz);
    if (!ioe_result.ok()) return from_ioe1(ioe_result);
    ioe_result = impl_->ioe1.configure_i2c(false, i2c_speed_hz);
    if (!ioe_result.ok()) return from_ioe1(ioe_result);

    gpio_config_t cs_config = {};
    cs_config.pin_bit_mask  = 1ULL << kPanelCs;
    cs_config.mode          = GPIO_MODE_OUTPUT;
    cs_config.pull_up_en    = GPIO_PULLUP_DISABLE;
    cs_config.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    cs_config.intr_type     = GPIO_INTR_DISABLE;
    esp_err_t gpio_result   = gpio_config(&cs_config);
    if (gpio_result != ESP_OK) return {gpio_result, gpio_result};
    gpio_result = gpio_set_level(kPanelCs, 1);
    if (gpio_result != ESP_OK) return {gpio_result, gpio_result};

    const auto rollback = [this](DisplayResult failure) {
        (void)impl_->ioe1.set_pin(m5sw::board::IoePin::display_reset, false);
        (void)impl_->ioe1.set_pin(m5sw::board::IoePin::l3b_enable, false);
        return failure;
    };

    ioe_result = impl_->ioe1.set_pin(m5sw::board::IoePin::display_reset, true);
    if (!ioe_result.ok()) return rollback(from_ioe1(ioe_result));
    ioe_result = impl_->ioe1.set_pin(m5sw::board::IoePin::l3b_enable, true);
    if (!ioe_result.ok()) return rollback(from_ioe1(ioe_result));
    vTaskDelay(pdMS_TO_TICKS(10));
    ioe_result = impl_->ioe1.set_pin(m5sw::board::IoePin::display_reset, false);
    if (!ioe_result.ok()) return rollback(from_ioe1(ioe_result));
    vTaskDelay(pdMS_TO_TICKS(8));
    ioe_result = impl_->ioe1.set_pin(m5sw::board::IoePin::display_reset, true);
    if (!ioe_result.ok()) return rollback(from_ioe1(ioe_result));
    vTaskDelay(pdMS_TO_TICKS(2));

    gpio_config_t te_config = {};
    te_config.pin_bit_mask  = 1ULL << kPanelTe;
    te_config.mode          = GPIO_MODE_INPUT;
    te_config.pull_up_en    = GPIO_PULLUP_ENABLE;
    te_config.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    te_config.intr_type     = GPIO_INTR_DISABLE;
    gpio_result             = gpio_config(&te_config);
    if (gpio_result != ESP_OK) return rollback({gpio_result, gpio_result});

    if (!impl_->init()) {
        impl_->releaseBus();
        return rollback({ESP_FAIL, ESP_FAIL});
    }
    if (!impl_->enable_framebuffer()) {
        impl_->releaseBus();
        return rollback({ESP_FAIL, ESP_FAIL});
    }

    impl_->setRotation(0);
    impl_->fillScreen(TFT_BLACK);
    initialized_ = true;
    return set_brightness_percent(50);
}

DisplayResult DisplayController::set_brightness_percent(std::uint8_t percent)
{
    if (!initialized_) return {ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_STATE};
    if (percent > 100U) return {ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_ARG};
    const std::uint8_t panel_value =
        static_cast<std::uint8_t>((static_cast<std::uint16_t>(percent) * 255U + 50U) / 100U);
    impl_->set_panel_brightness(panel_value);
    brightness_percent_ = percent;
    return {};
}

DisplayResult DisplayController::show_white_static()
{
    if (!initialized_) return {ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_STATE};
    impl_->fillScreen(TFT_WHITE);
    return {};
}

DisplayResult DisplayController::draw_normal_frame(std::uint32_t elapsed_seconds)
{
    if (!initialized_) return {ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_STATE};

    const std::uint32_t hours   = (elapsed_seconds / 3600U) % 100U;
    const std::uint32_t minutes = (elapsed_seconds / 60U) % 60U;
    const std::uint32_t seconds = elapsed_seconds % 60U;
    char time_text[16]          = {};
    std::snprintf(time_text, sizeof(time_text), "%02lu:%02lu:%02lu", static_cast<unsigned long>(hours),
                  static_cast<unsigned long>(minutes), static_cast<unsigned long>(seconds));

    impl_->draw_normal_frame(time_text);
    return {};
}

DisplayResult DisplayController::draw_full_load_step()
{
    if (!initialized_) return {ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_STATE};
    impl_->draw_full_load_step();
    return {};
}

}  // namespace m5sw::display
