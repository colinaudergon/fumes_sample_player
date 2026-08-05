#pragma once

#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "pico/types.h"
//
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/spi.h"

namespace filesystem::spi_interface
{
    struct SpiInterface
    {
        spi_inst_t *hw_inst;
        uint miso_gpio; // SPI MISO GPIO number (not pin number)
        uint mosi_gpio;
        uint sck_gpio;
        uint baud_rate;
    };

    class ISpiInterface
    {
    public:
        ISpiInterface() {};
        ~ISpiInterface() {};
        virtual bool Init() = 0;
        virtual void TransferStart(const uint8_t *tx, uint8_t *rx, size_t length) = 0;
        virtual uint32_t CalculateTransferTime(uint32_t bytes) = 0;
        virtual bool WaitTransferComplete(uint32_t timeout_ms) = 0;
        virtual bool Transfer(const uint8_t *tx, uint8_t *rx, size_t length) = 0;
        virtual uint GetSpiMode();
    };
} // namespace  filesystem::spi_interface
