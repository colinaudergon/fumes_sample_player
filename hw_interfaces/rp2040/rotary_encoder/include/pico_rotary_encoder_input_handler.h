/**
 * @file pico_rotary_encoder_input_handler.h
 * @brief C++ port of gv_lib's pico_gv_drivers/rotary_encoder C driver as an
 * hw_interface::IInputHandler implementation.
 *
 * Decodes a 2-pin (A/B) quadrature rotary encoder from GPIO edge interrupts, dispatched through
 * GpioInterruptDispatcher (see hw_interfaces/rp2040/gpio_interrupts), and reports each detected
 * step as a kNavigationEvent InputEvent (kUp for clockwise, kDown for counter-clockwise) --
 * suitable for use standalone or as one source registered with CompositeInputHandler alongside
 * other IInputHandler implementations.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "pico/stdlib.h"

#include "IInputHandler.h"

namespace hw_interface
{

    class PicoRotaryEncoderInputHandler : public IInputHandler
    {
    public:
        /// @param pin_a GPIO connected to the encoder's "A" quadrature channel.
        /// @param pin_b GPIO connected to the encoder's "B" quadrature channel.
        PicoRotaryEncoderInputHandler(uint pin_a, uint pin_b);
        ~PicoRotaryEncoderInputHandler() override = default;

        /// @brief Registers GPIO edge interrupts on both pins and captures their initial state.
        /// @return 0 on success, non-zero if either pin failed to register (see
        /// GpioInterruptDispatcher::RegisterHandler()).
        int Init() override;

        /// @brief Non-blocking: pops the oldest pending rotation step (if any) off the internal
        /// queue filled by the GPIO IRQ handler.
        bool PollEvent(InputEvent &out) override;

    private:
        // Small enough to absorb a burst of fast manual rotation between two PollEvent() calls
        // without dropping steps, without requiring dynamic allocation.
        static constexpr size_t kQueueCapacity = 16;

        const uint pin_a_;
        const uint pin_b_;

        // Packed (pin_a << 1 | pin_b) reading from the last processed edge; combined with the
        // newly read pins to look up the transition's direction (or "no direction", for
        // bounce/invalid transitions) -- same table as the original rotary_encoder.c.
        uint8_t quadrature_state_ = 0;

        // Ring buffer of pending direction events: pushed from GPIO IRQ context (HandleEdge()),
        // popped from PollEvent() on the main/UI thread. Access to the shared indices/count is
        // wrapped in save_and_disable_interrupts()/restore_interrupts() on both sides since
        // RP2040 has no atomic read-modify-write for plain integers across IRQ preemption.
        NavigationDirection event_queue_[kQueueCapacity]{};
        size_t queue_head_ = 0;
        size_t queue_tail_ = 0;
        size_t queue_count_ = 0;

        void HandleEdge();
        static void GpioIrqTrampoline(void *context);
    };

} // namespace hw_interface
