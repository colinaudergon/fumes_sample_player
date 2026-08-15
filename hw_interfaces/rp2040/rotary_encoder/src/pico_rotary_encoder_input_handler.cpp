#include "pico_rotary_encoder_input_handler.h"

#include "hardware/gpio.h"
#include "hardware/sync.h"

#include "gpio_interrupt_dispatcher.h"

namespace hw_interface
{

    namespace
    {
        // Packed (pin_a << 1 | pin_b) readings -- the 4 possible quadrature states of a 2-bit
        // Gray code, in the order they appear around the encoder's rotation cycle.
        constexpr uint8_t kStateAOffBOff = 0b00;
        constexpr uint8_t kStateAOffBOn = 0b01;
        constexpr uint8_t kStateAOnBOff = 0b10;
        constexpr uint8_t kStateAOnBOn = 0b11;

        // Packs a (previous_state, new_state) pair into the single byte HandleEdge() switches
        // on, so each switch case can be spelled in terms of the named states above instead of
        // a raw binary literal.
        constexpr uint8_t Transition(uint8_t previous_state, uint8_t new_state)
        {
            return static_cast<uint8_t>((previous_state << 2) | new_state);
        }
    } // namespace

    PicoRotaryEncoderInputHandler::PicoRotaryEncoderInputHandler(uint pin_a, uint pin_b)
        : pin_a_(pin_a), pin_b_(pin_b)
    {
    }

    int PicoRotaryEncoderInputHandler::Init()
    {
        constexpr uint32_t kBothEdges = GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL;

        const bool pin_a_registered = GpioInterruptDispatcher::RegisterHandler(pin_a_, kBothEdges, &GpioIrqTrampoline, this);
        const bool pin_b_registered = GpioInterruptDispatcher::RegisterHandler(pin_b_, kBothEdges, &GpioIrqTrampoline, this);
        if (!pin_a_registered || !pin_b_registered)
        {
            return -1;
        }

        quadrature_state_ = static_cast<uint8_t>((gpio_get(pin_a_) << 1) | gpio_get(pin_b_));
        return 0;
    }

    void PicoRotaryEncoderInputHandler::GpioIrqTrampoline(void *context)
    {
        static_cast<PicoRotaryEncoderInputHandler *>(context)->HandleEdge();
    }

    void PicoRotaryEncoderInputHandler::HandleEdge()
    {
        const uint8_t new_pins = static_cast<uint8_t>((gpio_get(pin_a_) << 1) | gpio_get(pin_b_));
        const uint8_t transition = static_cast<uint8_t>((quadrature_state_ << 2) | new_pins);

        // Same Gray-code transition table as the original rotary_encoder.c's handle_rotation():
        // each case is one of the 4 valid single-step transitions for a given direction; any
        // other combination is bounce/skipped-edge noise and is ignored.
        NavigationDirection direction;
        bool has_event = true;
        switch (transition)
        {
        case Transition(kStateAOffBOff, kStateAOffBOn):
        case Transition(kStateAOffBOn, kStateAOnBOn):
        case Transition(kStateAOnBOn, kStateAOnBOff):
        case Transition(kStateAOnBOff, kStateAOffBOff):
            direction = NavigationDirection::kUp;
            break;
        case Transition(kStateAOffBOn, kStateAOffBOff):
        case Transition(kStateAOnBOn, kStateAOffBOn):
        case Transition(kStateAOnBOff, kStateAOnBOn):
        case Transition(kStateAOffBOff, kStateAOnBOff):
            direction = NavigationDirection::kDown;
            break;
        default:
            has_event = false;
            break;
        }

        quadrature_state_ = new_pins;

        if (!has_event)
        {
            return;
        }

        const uint32_t interrupt_state = save_and_disable_interrupts();
        if (queue_count_ < kQueueCapacity)
        {
            event_queue_[queue_tail_] = direction;
            queue_tail_ = (queue_tail_ + 1) % kQueueCapacity;
            ++queue_count_;
        }
        // else: queue full -- drop the step rather than overwrite/block, matching
        // UserInterface::PushCommand()'s fixed-capacity queue behavior.
        restore_interrupts(interrupt_state);
    }

    bool PicoRotaryEncoderInputHandler::PollEvent(InputEvent &out)
    {
        const uint32_t interrupt_state = save_and_disable_interrupts();
        if (queue_count_ == 0)
        {
            restore_interrupts(interrupt_state);
            return false;
        }

        const NavigationDirection direction = event_queue_[queue_head_];
        queue_head_ = (queue_head_ + 1) % kQueueCapacity;
        --queue_count_;
        restore_interrupts(interrupt_state);

        out.type = InputEventType::kNavigationEvent;
        out.navigationDirection = direction;
        return true;
    }

} // namespace hw_interface
