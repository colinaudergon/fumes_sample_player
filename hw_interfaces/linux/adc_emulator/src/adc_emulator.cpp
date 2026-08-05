#include "adc_emulator.h"

#include <cstring>
#include <chrono>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/**
 * @file adc_emulator.cpp
 * @brief Emulator implementation of an ADC for native/Linux builds (no real adc hardware).
 */

namespace hw_interface
{

    namespace
    {
        constexpr uint8_t kCmdEndByte = static_cast<uint8_t>(AdcEmulatorCommandType::kCmdEnd);
    }

    AdcEmulator::~AdcEmulator()
    {
        StopReading();

        if (socket_fd_ >= 0)
        {
            ::close(socket_fd_);
            socket_fd_ = -1;
        }
    }

    int AdcEmulator::Init(const AdcConfig &config)
    {
        if (config.data == nullptr || config.size != sizeof(EmulatorAdcConfig))
        {
            return kAdcControllerErr;
        }

        EmulatorAdcConfig emulator_config;
        std::memcpy(&emulator_config, config.data, sizeof(EmulatorAdcConfig));

        return InitializeInternals(emulator_config);
    }

    int AdcEmulator::InitializeInternals(const EmulatorAdcConfig &emulator_config)
    {
        if (initialized_)
        {
            return kAdcControllerSuccess;
        }

        if (emulator_config.socket_path == nullptr || emulator_config.socket_path_size == 0)
        {
            return kAdcControllerErr;
        }

        const int connect_result = ConnectToSocket(emulator_config);
        if (connect_result != kAdcControllerSuccess)
        {
            return connect_result;
        }

        for (uint8 index = 0; index < kNbrAdc; ++index)
        {
            internal_adcs_[index].gpio = 0;
            internal_adcs_[index].adc_id = index;
            internal_adcs_[index].deadband_low_threshold = 0;
            internal_adcs_[index].deadband_high_threshold = 0;
            internal_adcs_[index].assigned_buffer_position = index;

            raw_reading_[index] = 0;
            normalized_reading_[index] = 0.0f;
            previous_valid_raw_reading_[index] = 0;
            has_previous_valid_reading_[index] = false;
        }

        initialized_ = true;
        last_reading_valid_ = false;
        next_adc_position_ = 0;

        return kAdcControllerSuccess;
    }

    int AdcEmulator::ConnectToSocket(const EmulatorAdcConfig &emulator_config)
    {
        if (socket_fd_ >= 0)
        {
            ::close(socket_fd_);
            socket_fd_ = -1;
        }

        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
        {
            return kAdcControllerErr;
        }

        sockaddr_un address{};
        address.sun_family = AF_UNIX;

        // socket_path_size includes the terminating NUL (see header doc); guard against
        // overflowing sun_path.
        if (emulator_config.socket_path_size > sizeof(address.sun_path))
        {
            ::close(fd);
            return kAdcControllerErr;
        }

        std::memcpy(address.sun_path, emulator_config.socket_path, emulator_config.socket_path_size);

        if (::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0)
        {
            ::close(fd);
            return kAdcControllerErr;
        }

        socket_fd_ = fd;
        return kAdcControllerSuccess;
    }

    int AdcEmulator::StartReading()
    {
        if (!initialized_)
        {
            return kAdcControllerErr;
        }

        if (is_reading_)
        {
            return kAdcControllerAlreadyReading;
        }

        is_reading_ = true;
        reading_thread_ = std::thread(&AdcEmulator::ReadingThreadMain, this);

        return kAdcControllerSuccess;
    }

    int AdcEmulator::StopReading()
    {
        if (!is_reading_)
        {
            return kAdcControllerSuccess;
        }

        is_reading_ = false;
        if (reading_thread_.joinable())
        {
            reading_thread_.join();
        }

        return kAdcControllerSuccess;
    }

    void AdcEmulator::ReadingThreadMain()
    {
        while (is_reading_)
        {
            ProcessSingleRoundRobinStep();
            std::this_thread::sleep_for(std::chrono::milliseconds(kBackgroundReadingPeriodMs));
        }
    }

    int AdcEmulator::SetAdcDeadBand(uint8 adc_id, uint16 deadband_low_threshold, uint16 deadband_high_threshold)
    {
        const int buffer_position = GetBufferPositionFromAdcId(adc_id);
        if (buffer_position < 0)
        {
            return (adc_id >= kNbrAdc) ? kAdcControllerAccessOutofBound : kAdcControllerErr;
        }

        std::lock_guard<std::mutex> lock(readings_mutex_);
        internal_adcs_[buffer_position].deadband_low_threshold = deadband_low_threshold;
        internal_adcs_[buffer_position].deadband_high_threshold = deadband_high_threshold;

        return kAdcControllerSuccess;
    }

    bool AdcEmulator::IsReadingValid()
    {
        return last_reading_valid_;
    }

    int AdcEmulator::GetAllNormalizedReading(float &buffer)
    {
        float *buffer_ptr = &buffer;
        std::lock_guard<std::mutex> lock(readings_mutex_);
        for (uint8 index = 0; index < kNbrAdc; ++index)
        {
            buffer_ptr[index] = normalized_reading_[index];
        }

        return kAdcControllerSuccess;
    }

    int AdcEmulator::GetNormalizedReading(uint8 adc_id, float &normalized_value)
    {
        const int buffer_position = GetBufferPositionFromAdcId(adc_id);
        if (buffer_position < 0)
        {
            return (adc_id >= kNbrAdc) ? kAdcControllerAccessOutofBound : kAdcControllerErr;
        }

        std::lock_guard<std::mutex> lock(readings_mutex_);
        normalized_value = normalized_reading_[buffer_position];
        return kAdcControllerSuccess;
    }

    int AdcEmulator::GetAllRawReading(uint16 &buffer)
    {
        uint16 *buffer_ptr = &buffer;
        std::lock_guard<std::mutex> lock(readings_mutex_);
        for (uint8 index = 0; index < kNbrAdc; ++index)
        {
            buffer_ptr[index] = raw_reading_[index];
        }

        return kAdcControllerSuccess;
    }

    int AdcEmulator::GetRawReading(uint8 adc_id, uint16 &raw_value)
    {
        const int buffer_position = GetBufferPositionFromAdcId(adc_id);
        if (buffer_position < 0)
        {
            return (adc_id >= kNbrAdc) ? kAdcControllerAccessOutofBound : kAdcControllerErr;
        }

        std::lock_guard<std::mutex> lock(readings_mutex_);
        raw_value = raw_reading_[buffer_position];
        return kAdcControllerSuccess;
    }

    int AdcEmulator::GetBufferPositionFromAdcId(uint8 adc_id) const
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

    void AdcEmulator::ProcessSingleRoundRobinStep()
    {
        if (!initialized_)
        {
            last_reading_valid_ = false;
            return;
        }

        const uint8 adc_position = next_adc_position_;
        const uint8 adc_id = internal_adcs_[adc_position].adc_id;

        uint16 new_raw_reading = 0;
        const int request_result = RequestRawValue(adc_id, new_raw_reading);
        if (request_result != kAdcControllerSuccess)
        {
            last_reading_valid_ = false;
            next_adc_position_ = static_cast<uint8>((next_adc_position_ + 1) % kNbrAdc);
            return;
        }

        std::lock_guard<std::mutex> lock(readings_mutex_);

        const Adc &adc = internal_adcs_[adc_position];
        bool is_valid = false;

        if (!has_previous_valid_reading_[adc_position])
        {
            is_valid = true;
        }
        else
        {
            const uint16 previous_raw_reading = previous_valid_raw_reading_[adc_position];
            const uint16 lower_bound = (previous_raw_reading > adc.deadband_low_threshold)
                                             ? static_cast<uint16>(previous_raw_reading - adc.deadband_low_threshold)
                                             : 0;

            uint32 upper_bound_u32 = static_cast<uint32>(previous_raw_reading) + adc.deadband_high_threshold;
            if (upper_bound_u32 > kAdcMaxValue)
            {
                upper_bound_u32 = kAdcMaxValue;
            }
            const uint16 upper_bound = static_cast<uint16>(upper_bound_u32);

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
        next_adc_position_ = static_cast<uint8>((next_adc_position_ + 1) % kNbrAdc);
    }

    int AdcEmulator::RequestRawValue(uint8 adc_id, uint16 &raw_value)
    {
        if (socket_fd_ < 0)
        {
            return kAdcControllerErr;
        }

        AdcEmulatorMessage request{};
        request.cmd_type = static_cast<uint8_t>(AdcEmulatorCommandType::kCmdReq);
        request.cmd_id = adc_id;
        request.value = 0;

        uint8_t send_buffer[kCmdBufferSize];
        SerializeMessage(request, send_buffer);

        if (!SendAll(socket_fd_, send_buffer, kCmdBufferSize))
        {
            return kAdcControllerErr;
        }

        uint8_t recv_buffer[kCmdBufferSize];
        if (!RecvAll(socket_fd_, recv_buffer, kCmdBufferSize))
        {
            return kAdcControllerErr;
        }

        AdcEmulatorMessage response{};
        if (!DeserializeMessage(recv_buffer, response))
        {
            return kAdcControllerErr;
        }

        if (response.cmd_type != static_cast<uint8_t>(AdcEmulatorCommandType::kCmdVal) ||
            response.cmd_id != adc_id)
        {
            return kAdcControllerErr;
        }

        // The remote process only returns raw 12-bit readings; this class handles normalization.
        raw_value = static_cast<uint16>(response.value & 0x0FFF);
        return kAdcControllerSuccess;
    }

    void AdcEmulator::SerializeMessage(const AdcEmulatorMessage &message, uint8_t (&buffer)[kCmdBufferSize])
    {
        buffer[0] = message.cmd_type;
        buffer[1] = message.cmd_id;
        buffer[2] = static_cast<uint8_t>(message.value & 0x00FF);
        buffer[3] = static_cast<uint8_t>((message.value >> 8) & 0x00FF);
        buffer[4] = kCmdEndByte;
    }

    bool AdcEmulator::DeserializeMessage(const uint8_t (&buffer)[kCmdBufferSize], AdcEmulatorMessage &message)
    {
        if (buffer[4] != kCmdEndByte)
        {
            return false;
        }

        message.cmd_type = buffer[0];
        message.cmd_id = buffer[1];
        message.value = static_cast<uint16_t>(buffer[2]) | (static_cast<uint16_t>(buffer[3]) << 8);

        return true;
    }

    bool AdcEmulator::SendAll(int fd, const uint8_t *data, size_t size)
    {
        size_t sent = 0;
        while (sent < size)
        {
            const ssize_t result = ::send(fd, data + sent, size - sent, 0);
            if (result <= 0)
            {
                return false;
            }
            sent += static_cast<size_t>(result);
        }

        return true;
    }

    bool AdcEmulator::RecvAll(int fd, uint8_t *data, size_t size)
    {
        size_t received = 0;
        while (received < size)
        {
            const ssize_t result = ::recv(fd, data + received, size - received, 0);
            if (result <= 0)
            {
                return false;
            }
            received += static_cast<size_t>(result);
        }

        return true;
    }

} // namespace hw_interface
