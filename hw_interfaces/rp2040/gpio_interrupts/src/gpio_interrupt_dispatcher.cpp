#include "gpio_interrupt_dispatcher.h"

#include "hardware/gpio.h"

namespace hw_interface
{

    GpioInterruptDispatcher::Entry GpioInterruptDispatcher::handlers_[kNumGpioPins]{};

    bool GpioInterruptDispatcher::RegisterHandler(uint pin, uint32_t condition, GpioIrqHandler fn, void *context)
    {
        if (pin >= kNumGpioPins || fn == nullptr)
        {
            return false;
        }

        gpio_init(pin);
        gpio_pull_up(pin);

        handlers_[pin].fn = fn;
        handlers_[pin].context = context;

        // Re-registering HandleInterrupt for every pin is harmless: the Pico SDK just keeps
        // pointing its single shared callback at the same function.
        gpio_set_irq_enabled_with_callback(pin, condition, true, &HandleInterrupt);
        return true;
    }

    void GpioInterruptDispatcher::HandleInterrupt(uint gpio, uint32_t events)
    {
        (void)events; // Handlers decide relevance themselves (e.g. by re-reading pin state).

        if (gpio >= kNumGpioPins)
        {
            return;
        }

        const Entry &entry = handlers_[gpio];
        if (entry.fn != nullptr)
        {
            entry.fn(entry.context);
        }
    }

} // namespace hw_interface
