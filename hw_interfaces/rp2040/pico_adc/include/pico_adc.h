/**
 * @file pico_adc.h
 * @brief
 */

#pragma once

#include <cstdint>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "../../../include/IAdcController.h"

namespace hw_interface
{

    // Hardware-specific configuration consumed by PicoAdcController::Init(), packed by the
    // caller into an AdcConfig{data, size} pair (see IAdcController::Init()):
    //   PicoAdcConfig cfg{26, 27, 28, 29};
    //   adc_controller.Init(hw_interface::AdcConfig{&cfg, sizeof(cfg)});
    struct PicoAdcConfig
    {
        uint adc_1_gpio;
        uint adc_2_gpio;
        uint adc_3_gpio;
        uint adc_4_gpio;
    };

    // Concrete IAdcController implementation for the RP2040's on-board ADC.
    class PicoAdcController: public IAdcController
    {
    public:
        PicoAdcController() = default;
        ~PicoAdcController() override = default;

        int Init(const AdcConfig &config) override;
        int StartReading() override;
        int StopReading() override;
        int SetAdcDeadBand(uint8_t adc_id, uint16_t deadband_low_threshold, uint16_t deadband_high_threshold) override;
        bool IsReadingValid() override;

        int GetAllNormalizedReading(float& buffer) override;
        int GetNormalizedReading(uint8_t adc_id, float& normalized_value) override;

        int GetAllRawReading(uint16_t& buffer) override;
        int GetRawReading(uint8_t adc_id, uint16_t& raw_value) override;

    private:
        static constexpr uint8_t kNbrAdc = 4;
        static constexpr uint16_t kAdcMaxValue = 4095;
        static constexpr int32_t kBackgroundReadingPeriodUs = -1000;

        Adc internal_adcs_[kNbrAdc];
        uint16_t raw_reading_[kNbrAdc];
        float normalized_reading_[kNbrAdc];
        uint16_t previous_valid_raw_reading_[kNbrAdc];
        bool has_previous_valid_reading_[kNbrAdc];

        bool initialized_ = false;
        bool is_reading_ = false;
        bool last_reading_valid_ = false;
        uint8_t next_adc_position_ = 0;
        repeating_timer_t reading_timer_;

        int GetBufferPositionFromAdcId(uint8_t adc_id) const;
        void ProcessSingleRoundRobinStep();
        static bool ReadingTimerCallback(repeating_timer_t *timer);

    };

} // namespace hw_interface
