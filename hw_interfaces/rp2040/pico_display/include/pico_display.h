/**
 * @file pico_display.h
 * @brief hw_interface::IDisplay implementation driving a u8g2-supported SPI display
 * (SSD1306 128x64, SPI1) on the RP2040, built on the u8g2 library
 * (https://github.com/olikraus/u8g2, vendored at lib/u8g2) and the SPI1 u8x8 glue in
 * pico_display_u8x8_spi1.h -- ported from
 * https://github.com/mwinters-stuff/u8g2-raspberrypi-pico-cpp-sdk-play.
 */

#pragma once

#include <cstdint>

#include "pico/stdlib.h"

#include "u8g2.h"

#include "IDisplay.h"

namespace hw_interface
{

    typedef uint8_t ug8_color_t;

    class PicoDisplay : public IDisplay
    {
    public:
        /// @brief Identifies each DisplayArea below; compare against DisplayArea::id (e.g. to
        /// tell which area last received an input event, or to route a redraw to a specific
        /// area) instead of a raw/manually-assigned integer.
        enum class DisplayAreaId : uint8_t
        {
            kScreen = 0,
            kLivePlayMenuArea,
            kLiveViewArea,
            kInfoArea,
            kFileSelectMenuArea,
        };

        enum class DisplayAreaIds : uint8_t
        {
            kNone,
            kRootScreen,
            kLiveViewMenu,

        };

        struct DisplayArea
        {
            void *parent;
            size_t x_pos;
            size_t y_pos;

            size_t width;
            size_t height;
            DisplayAreaId id;
        };

        /// @brief GPIO/SPI configuration for the display. Defaults use SPI1 on GPIO26 (SCK) /
        /// GPIO27 (MOSI), with CS/DC/RESET as plain GPIO outputs on GPIO22/21/20 -- chosen to
        /// avoid the GPIOs already claimed elsewhere in this proje ct (status LED: GPIO0; SD
        /// card SPI0: GPIO4/6/7/8; PWM audio out: GPIO10/12).
        struct Config
        {
            uint sck_gpio = 14;
            uint mosi_gpio = 15;
            uint cs_gpio = 1;
            uint dc_gpio = 3;
            uint reset_gpio = 2;
            uint32_t spi_baudrate_hz = 10000000;
        };

        PicoDisplay();
        explicit PicoDisplay(const Config &config);
        ~PicoDisplay() override = default;

        /// @brief Configures SPI1 + GPIOs (via pico_display_u8x8_spi1.h) and brings up the
        /// display through u8g2.
        /// @return 0 on success. u8g2's own init path has no failure signaling, so this always
        /// succeeds once called; kept non-void to match IDisplay::Init()'s contract.
        int Init() override;

        /// @brief Clears the display and draws `text` as a single left-aligned line.
        int ShowText(const char *text) override;

        /// @brief Clears the display and draws `file_name` and a "Duration: <ms> ms" line
        /// beneath it.
        int DisplayFileInfo(const char *file_name, uint32_t duration_ms) override;

        int DisplayAudioBufferContent(float *audio_left, float *audio_right, size_t n_frames) override;

    private:
        Config config_;
        u8g2_t u8g2_{};
        bool initialized_ = false;

        void RenderLine(const char *line);

        void ClearArea(const DisplayArea &area);

        static constexpr size_t kScreenWidth = 128;
        static constexpr size_t kScreenHeight = 64;

        static constexpr DisplayArea kScreen = {
            .parent = nullptr,
            .x_pos = 0,
            .y_pos = 0,
            .width = kScreenWidth,
            .height = kScreenHeight,
            .id = DisplayAreaId::kScreen};

        static constexpr DisplayArea kLivePlayMenuArea =
            {
                .parent = const_cast<void *>(static_cast<const void *>(&kScreen)),
                .x_pos = 0,
                .y_pos = 0,
                .width = kScreenWidth,
                .height = kScreenHeight,
                .id = DisplayAreaId::kLivePlayMenuArea,
            };

        static constexpr uint8_t kLiveViewAreaHeight = 43;
        static constexpr uint8_t kLiveViewAreaY = kScreenHeight - kLiveViewAreaHeight;

        static constexpr DisplayArea kLiveViewArea = {
            .parent = const_cast<void *>(static_cast<const void *>(&kScreen)),
            .x_pos = 0,
            .y_pos = kLiveViewAreaY,
            .width = kScreenWidth,
            .height = kLiveViewAreaHeight,
            .id = DisplayAreaId::kLiveViewArea};

        static constexpr DisplayArea kInfoArea = {
            .parent = const_cast<void *>(static_cast<const void *>(&kScreen)),
            .x_pos = 0,
            .y_pos = 0,
            .width = kScreenWidth,
            .height = kScreenHeight - kLiveViewAreaHeight,
            .id = DisplayAreaId::kInfoArea};

        static constexpr DisplayArea kFileSelectMenuArea = {
            .parent = const_cast<void *>(static_cast<const void *>(&kScreen)),
            .x_pos = 0,
            .y_pos = 0,
            .width = kScreenWidth,
            .height = kScreenHeight,
            .id = DisplayAreaId::kFileSelectMenuArea,
        };

        static constexpr ug8_color_t kUg8Black = 0;
        static constexpr ug8_color_t kUg8White = 1;
    };

} // namespace hw_interface
