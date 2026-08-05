#pragma once

#include "third_party/sd_card_constants.h"

namespace filesystem
{
    enum class SdInterface : uint8_t
    {
        kSpi,
        kSdio // currently unuspported and outside of scope
    };

    typedef struct
    {
        DSTATUS m_Status;      // Card status
        card_type_t card_type; // Assigned dynamically
        CSD_t CSD;             // Card-Specific Data register.
        CID_t CID;             // Card IDentification register
        uint32_t sectors;      // Assigned dynamically

        mutex_t mutex;
        FATFS fatfs;
        bool mounted;

        char drive_prefix[4];
    } sd_card_state_t;

    typedef struct
    {
        bool use_card_detect;
        uint card_detect_gpio;   // Card detect; ignored if !use_card_detect
        uint card_detected_true; // Varies with card socket; ignored if !use_card_detect
        bool card_detect_use_pull;
        bool card_detect_pull_hi;

    } card_detect_t;

    class ISdCard
    {
    public:
        SdCard() {};
        ~SdCard() {};
        int InitDriver() = 0;
        int CardDetect() = 0;
        void CiDump() = 0;
        void CsdDump() = 0;
        const char *GetDrivePrefix() = 0;
    };

    class SdCardSPi : public ISdCard
    {
    public:
        SdCardSPi() override = default;
        ~SdCardSPi() = default;

        int InitDriver() override;
        int CardDetect() override;
        void CiDump() override;
        void CsdDump() override;
        const char *GetDrivePrefix() override;

    private:
        sd_card_state_t state_;
        card_detect_t card_detect_;
        bool driver_initialized_;
        uin8_t number_of_sd_card_;
        size_t driver_number_;
        void Lock();
        void Unlock();
        bool IsLocked();
        void SetDrivePrefix();
    };
} // namespace filesystem