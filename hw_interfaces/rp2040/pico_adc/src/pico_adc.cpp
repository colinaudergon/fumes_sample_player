#include "pico_adc.h"

#include <cstring>

namespace hw_interface
{

    namespace
    {
        constexpr uint8_t kAdcGpioMin = 26;
        constexpr uint8_t kAdcGpioMax = 29;
    }

    int PicoAdcController::Init(const AdcConfig &config)
    {
        if (config.data == nullptr || config.size != sizeof(PicoAdcConfig))
        {
            return kAdcControllerErr;
        }

        PicoAdcConfig pico_config;
        std::memcpy(&pico_config, config.data, sizeof(PicoAdcConfig));

        const uint adc_gpio_map[kNbrAdc] = {pico_config.adc_1_gpio, pico_config.adc_2_gpio,
                                             pico_config.adc_3_gpio, pico_config.adc_4_gpio};

        for (uint8_t index = 0; index < kNbrAdc; ++index)
        {
            if ((adc_gpio_map[index] < kAdcGpioMin) || (adc_gpio_map[index] > kAdcGpioMax))
            {
                return kAdcControllerErr;
            }
        }

        adc_init();

        for (uint8_t index = 0; index < kNbrAdc; ++index)
        {
            internal_adcs_[index].gpio = adc_gpio_map[index];
            internal_adcs_[index].adc_id = static_cast<uint8_t>(adc_gpio_map[index] - kAdcGpioMin);
            internal_adcs_[index].deadband_low_threshold = 0;
            internal_adcs_[index].deadband_high_threshold = 0;
            internal_adcs_[index].assigned_buffer_position = index;

            raw_reading_[index] = 0;
            normalized_reading_[index] = 0.0f;
            previous_valid_raw_reading_[index] = 0;
            has_previous_valid_reading_[index] = false;

            adc_gpio_init(adc_gpio_map[index]);
        }

        initialized_ = true;
        is_reading_ = false;
        last_reading_valid_ = false;
        next_adc_position_ = 0;

        return kAdcControllerSuccess;
    }

    int PicoAdcController::StartReading()
    {
        if (!initialized_)
        {
            return kAdcControllerErr;
        }

        if (is_reading_)
        {
            return kAdcControllerAlreadyReading;
        }

        if (!add_repeating_timer_us(kBackgroundReadingPeriodUs, ReadingTimerCallback, this, &reading_timer_))
        {
            return kAdcControllerErr;
        }

        is_reading_ = true;
        return kAdcControllerSuccess;
    }

    int PicoAdcController::StopReading()
    {
        if (!is_reading_)
        {
            return kAdcControllerSuccess;
        }

        cancel_repeating_timer(&reading_timer_);
        is_reading_ = false;
        return kAdcControllerSuccess;
    }

    int PicoAdcController::SetAdcDeadBand(uint8_t adc_id, uint16_t deadband_low_threshold, uint16_t deadband_high_threshold)
    {
        const int buffer_position = GetBufferPositionFromAdcId(adc_id);
        if (buffer_position < 0)
        {
            return (adc_id >= kNbrAdc) ? kAdcControllerAccessOutofBound : kAdcControllerErr;
        }

        internal_adcs_[buffer_position].deadband_low_threshold = deadband_low_threshold;
        internal_adcs_[buffer_position].deadband_high_threshold = deadband_high_threshold;

        return kAdcControllerSuccess;
    }

    bool PicoAdcController::IsReadingValid()
    {
        return last_reading_valid_;
    }

    int PicoAdcController::GetAllNormalizedReading(float &buffer)
    {
        float *buffer_ptr = &buffer;
        for (uint8_t index = 0; index < kNbrAdc; ++index)
        {
            buffer_ptr[index] = normalized_reading_[index];
        }

        return kAdcControllerSuccess;
    }

    int PicoAdcController::GetNormalizedReading(uint8_t adc_id, float &normalized_value)
    {
        const int buffer_position = GetBufferPositionFromAdcId(adc_id);
        if (buffer_position < 0)
        {
            return (adc_id >= kNbrAdc) ? kAdcControllerAccessOutofBound : kAdcControllerErr;
        }

        normalized_value = normalized_reading_[buffer_position];
        return kAdcControllerSuccess;
    }

    int PicoAdcController::GetAllRawReading(uint16_t &buffer)
    {
        uint16_t *buffer_ptr = &buffer;
        for (uint8_t index = 0; index < kNbrAdc; ++index)
        {
            buffer_ptr[index] = raw_reading_[index];
        }

        return kAdcControllerSuccess;
    }

    int PicoAdcController::GetRawReading(uint8_t adc_id, uint16_t &raw_value)
    {
        const int buffer_position = GetBufferPositionFromAdcId(adc_id);
        if (buffer_position < 0)
        {
            return (adc_id >= kNbrAdc) ? kAdcControllerAccessOutofBound : kAdcControllerErr;
        }

        raw_value = raw_reading_[buffer_position];
        return kAdcControllerSuccess;
    }

    int PicoAdcController::GetBufferPositionFromAdcId(uint8_t adc_id) const
    {
        for (const auto &adc : internal_adcs_)
        {
            if (adc_id == adc.adc_id)
            {
                return adc.assigned_buffer_position;
            }
        }

        return -1;
    }

    void PicoAdcController::ProcessSingleRoundRobinStep()
    {
        if (!initialized_)
        {
            last_reading_valid_ = false;
            return;
        }

        const uint8_t adc_position = next_adc_position_;
        const Adc &adc = internal_adcs_[adc_position];

        adc_select_input(adc.adc_id);
        const uint16_t new_raw_reading = adc_read();

        bool is_valid = false;

        if (!has_previous_valid_reading_[adc_position])
        {
            is_valid = true;
        }
        else
        {
            const uint16_t previous_raw_reading = previous_valid_raw_reading_[adc_position];
            const uint16_t lower_bound = (previous_raw_reading > adc.deadband_low_threshold)
                                             ? static_cast<uint16_t>(previous_raw_reading - adc.deadband_low_threshold)
                                             : 0;

            uint32_t upper_bound_u32 = static_cast<uint32_t>(previous_raw_reading) + adc.deadband_high_threshold;
            if (upper_bound_u32 > kAdcMaxValue)
            {
                upper_bound_u32 = kAdcMaxValue;
            }
            const uint16_t upper_bound = static_cast<uint16_t>(upper_bound_u32);

            is_valid = (new_raw_reading < lower_bound) || (new_raw_reading > upper_bound);
        }

        if (is_valid)
        {
            raw_reading_[adc_position] = new_raw_reading;
            normalized_reading_[adc_position] = static_cast<float>(new_raw_reading) / static_cast<float>(kAdcMaxValue);
            previous_valid_raw_reading_[adc_position] = new_raw_reading;
            has_previous_valid_reading_[adc_position] = true;
        }

        last_reading_valid_ = is_valid;
        next_adc_position_ = static_cast<uint8_t>((next_adc_position_ + 1) % kNbrAdc);
    }

    bool PicoAdcController::ReadingTimerCallback(repeating_timer_t *timer)
    {
        auto *adc_controller = static_cast<PicoAdcController *>(timer->user_data);
        if (adc_controller == nullptr)
        {
            return false;
        }

        adc_controller->ProcessSingleRoundRobinStep();
        return true;
    }
} // namespace hw_interface
