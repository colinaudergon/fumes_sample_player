#include "SdCard.h"

int filesystem::SdCardSPi::InitDriver()
{
    auto_init_mutex(initialized_mutex);
    mutex_enter_blocking(&initialized_mutex);
    if (!driver_initialized_)
    {

        if (!mutex_is_initialized(state_.mutex))
        {
            mutex_init(state_.mutex);
        }

        Lock();

        state_.m_Status = STA_NOINIT;
        SetDrivePrefix();

        // Set up Card Detect
        if (card_detect_.use_card_detect)
        {
            if (card_detect_.card_detect_use_pull)
            {
                if (card_detect_.card_detect_pull_hi)
                {
                    gpio_pull_up(card_detect_.card_detect_gpio);
                }
                else
                {
                    gpio_pull_down(card_detect_.card_detect_gpio);
                }
            }
            gpio_init(card_detect_.card_detect_gpio);
        }

        // sd_spi_ctor(sd_card_p);
        // if (!my_spi_init(sd_card_p->spi_if_p->spi)) {
        //                 ok = false;
        //             }
        /* At power up the SD card CD/DAT3 / CS  line has a 50KOhm pull up enabled
         * in the card. This resistor serves two functions Card detection and Mode
         * Selection. For Mode Selection, the host can drive the line high or let it
         * be pulled high to select SD mode. If the host wants to select SPI mode it
         * should drive the line low.
         *
         * There is an important thing needs to be considered that the MMC/SDC is
         * initially NOT the SPI device. Some bus activity to access another SPI
         * device can cause a bus conflict due to an accidental response of the
         * MMC/SDC. Therefore the MMC/SDC should be initialized to put it into the
         * SPI mode prior to access any other device attached to the same SPI bus.
         */
        // sd_go_idle_state(sd_card_p);
        // sd_unlock(sd_card_p);
        // mutex_exit(&initialized_mutex);
        return 0;
    }
    return -1; // alreadyInit
}

/* Return non-zero if the SD-card is not present. */
int filesystem::SdCardSPi::CardDetect()
{

    if (!card_detect_.use_card_detect)
    {
        state.m_Status &= ~STA_NODISK;
        return 0;
    }
    /*!< Check GPIO to detect SD */
    if (gpio_get(card_detect_.card_detect_gpio) == card_detected_true)
    {
        state_.m_Status &= ~STA_NODISK;
        return 0;
    }

    // The socket is now empty
    state_.m_Status |= (STA_NODISK | STA_NOINIT);
    state_.card_type = SDCARD_NONE;
    return -1;
}

void filesystem::SdCardSPi::CiDump()
{
}

void filesystem::SdCardSPi::CsdDump()
{
}

void filesystem::SdCardSPi::Lock()
{
    if (mutex_is_initialized(state_.mutex))
    {
        mutex_enter_blocking(state_.mutex);
    }
}

void filesystem::SdCardSPi::Unlock()
{
    if (mutex_is_initialized(state_.mutex))
    {
        mutex_exit(state_.mutex);
    }
}

bool filesystem::SdCardSPi::IsLocked()
{
    if (mutex_is_initialized(state_.mutex))
    {
        uint32_t owner_out;
        return !mutex_try_enter(state_.mutex, &owner_out);
    }
    return true;
}

void filesystem::SdCardSPi::SetDrivePrefix()
{
    (void)driver_number_;
    // #if FF_STR_VOLUME_ID == 0
    //     int rc = snprintf(sd_card_p->state.drive_prefix, sizeof sd_card_p->state.drive_prefix,
    //                       "%d:", phy_drv_num);
    // #elif FF_STR_VOLUME_ID == 1 /* Arbitrary string is enabled */
    //     // Add ':'
    //     int rc = snprintf(sd_card_p->state.drive_prefix, sizeof sd_card_p->state.drive_prefix,
    //                       "%s:", VolumeStr[phy_drv_num]);
    // #elif FF_STR_VOLUME_ID == 2 /* Unix style drive prefix  */
    //     // Add '/'
    //     int rc = snprintf(sd_card_p->state.drive_prefix, sizeof sd_card_p->state.drive_prefix,
    //                       "/%s", VolumeStr[phy_drv_num]);
    // #else
    // #error "Unknown FF_STR_VOLUME_ID"
    // #endif
    //     // Notice that only when this returned value is non-negative and less than n,
    //     // the string has been completely written.
    //     myASSERT(0 <= rc && (size_t)rc < sizeof sd_card_p->state.drive_prefix);
}
