/**
 * @file adc_emulator.h
 * @brief
 */

#pragma once

#include <atomic>
#include <mutex>
#include <thread>

#include "../../../include/IAdcController.h"

namespace hw_interface
{

    // Hardware-specific configuration consumed by AdcEmulator::Init(), packed by the caller
    // into an AdcConfig{data, size} pair (see IAdcController::Init()):
    //   const char *socket_path = "/tmp/adc_emulator.sock";
    //   EmulatorAdcConfig cfg{socket_path, strlen(socket_path) + 1, nullptr, 0};
    //   adc_controller.Init(hw_interface::AdcConfig{&cfg, sizeof(cfg)});
    // `socket_path` must point to a NUL-terminated path to a Unix domain socket exposed by the
    // remote process providing raw ADC values. `process_name`/`process_name_size` are reserved
    // for future use (e.g. spawning/identifying the remote process) and are currently unused.
    // Neither pointer is retained past Init(): both are only dereferenced while connecting.
    struct EmulatorAdcConfig
    {
        const char *socket_path;
        size_t socket_path_size;
        const char *process_name;
        size_t process_name_size;
    };

    // Wire-level command types exchanged with the remote process over the Unix domain socket.
    // Every message is a fixed 5-byte frame: [cmd_type][cmd_id][value_lo][value_hi][cmd_end].
    // `kCmdEnd`'s numeric value doubles as the frame terminator byte placed in the 5th position
    // -- it is never sent as an actual `cmd_type`.
    enum class AdcEmulatorCommandType : uint8_t
    {
        kCmdOk = 0,
        kCmdVal = 1,
        kCmdReq = 2,
        kCmdEnd = 3,
        kCmdErr = 4,
    };

    // In-memory representation of a single 5-byte frame. `cmd_id` directly identifies the ADC
    // channel (0..kNbrAdc-1). `value` only carries meaning on a `kCmdVal` response: it holds the
    // remote process' raw 12-bit reading (0..4095).
    struct AdcEmulatorMessage
    {
        uint8_t cmd_type;
        uint8_t cmd_id;
        uint16_t value;
    };

    // Concrete IAdcController implementation for native/Linux builds: emulates an ADC by
    // requesting raw values from an external process over a Unix domain socket. The remote
    // process only returns raw 12-bit readings ([cmd_type=kCmdVal][cmd_id][value][kCmdEnd]);
    // this class applies the same deadband-filtering/normalization behavior as
    // hw_interface::PicoAdcController, sourcing raw values via the socket instead of real
    // hardware, sampled round-robin from a background thread.
    class AdcEmulator : public IAdcController
    {
    public:
        AdcEmulator() = default;
        ~AdcEmulator() override;

        int Init(const AdcConfig &config) override;
        int StartReading() override;
        int StopReading() override;
        int SetAdcDeadBand(uint8 adc_id, uint16 deadband_low_threshold, uint16 deadband_high_threshold) override;
        bool IsReadingValid() override;

        int GetAllNormalizedReading(float &buffer) override;
        int GetNormalizedReading(uint8 adc_id, float &normalized_value) override;

        int GetAllRawReading(uint16 &buffer) override;
        int GetRawReading(uint8 adc_id, uint16 &raw_value) override;

    private:
        static constexpr uint8 kNbrAdc = 4;
        static constexpr uint16 kAdcMaxValue = 4095;
        static constexpr int kBackgroundReadingPeriodMs = 10;
        // Frame layout: cmd_type(1) + cmd_id(1) + value(2, little-endian) + cmd_end(1).
        static constexpr size_t kCmdBufferSize = 5;

        Adc internal_adcs_[kNbrAdc];
        uint16 raw_reading_[kNbrAdc] = {};
        float normalized_reading_[kNbrAdc] = {};
        uint16 previous_valid_raw_reading_[kNbrAdc] = {};
        bool has_previous_valid_reading_[kNbrAdc] = {};

        bool initialized_ = false;
        std::atomic<bool> is_reading_{false};
        bool last_reading_valid_ = false;
        uint8 next_adc_position_ = 0;

        int socket_fd_ = -1;
        std::mutex readings_mutex_;
        std::thread reading_thread_;

        int InitializeInternals(const EmulatorAdcConfig &emulator_config);
        int ConnectToSocket(const EmulatorAdcConfig &emulator_config);
        int RequestRawValue(uint8 adc_id, uint16 &raw_value);
        int GetBufferPositionFromAdcId(uint8 adc_id) const;
        void ProcessSingleRoundRobinStep();
        void ReadingThreadMain();

        static void SerializeMessage(const AdcEmulatorMessage &message, uint8_t (&buffer)[kCmdBufferSize]);
        static bool DeserializeMessage(const uint8_t (&buffer)[kCmdBufferSize], AdcEmulatorMessage &message);

        static bool SendAll(int fd, const uint8_t *data, size_t size);
        static bool RecvAll(int fd, uint8_t *data, size_t size);
    };

} // namespace hw_interface
