
#pragma once

#include <cstddef>
#include <cstdint>

namespace hw_interface
{

    struct Adc
    {
        uint gpio;
        uint8_t adc_id;
        uint16_t deadband_low_threshold;
        uint16_t deadband_high_threshold;
        uint8_t assigned_buffer_position;
    };

    // Opaque, implementation-defined configuration blob passed to IAdcController::Init().
    // The interface itself never interprets the bytes: each concrete IAdcController packs
    // (caller side) and unpacks (implementation side) whatever layout it needs -- e.g. a set
    // of GPIO numbers for an on-chip ADC, an I2C address/channel mask for an external ADC,
    // nothing at all for a mock/test implementation, etc. This keeps the interface free of any
    // assumption about the underlying hardware.
    struct AdcConfig
    {
        const void *data = nullptr;
        size_t size = 0;
    };

    class IAdcController
    {
    public:
        virtual ~IAdcController() = default;

        virtual int Init(const AdcConfig &config) = 0;
        virtual int StartReading() = 0;
        virtual int StopReading() = 0;
        virtual int SetAdcDeadBand(uint8_t adc_id, uint16_t deadband_low_threshold, uint16_t deadband_high_threshold) = 0;
        virtual bool IsReadingValid() = 0;
        
        virtual int GetAllNormalizedReading(float& buffer) = 0;
        virtual int GetNormalizedReading(uint8_t adc_id, float& normalized_value) = 0;
        
        virtual int GetAllRawReading(uint16_t& buffer) = 0;
        virtual int GetRawReading(uint8_t adc_id,uint16_t& raw_value) = 0;
        
        static constexpr int kAdcControllerSuccess = 0;
        static constexpr int kAdcControllerErr = -1;
        static constexpr int kAdcControllerAlreadyReading = -2;
        static constexpr int kAdcControllerAccessOutofBound = -3;
    };

} // namespace hw_interface
