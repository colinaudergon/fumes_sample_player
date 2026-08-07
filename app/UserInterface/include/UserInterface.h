/**
 * @file UserInterface.h
 * @brief
 */

#pragma once

#include <cstddef>
#include "../../../hw_interfaces/include/IInputHandler.h"
#include "../../../hw_interfaces/include/IDisplay.h"

namespace app::ui
{
    class UserInterface
    {
    public:
        UserInterface(hw_interface::IInputHandler &input_handler, hw_interface::IDisplay &display): input_handler_(input_handler), display_(display) {};
        ~UserInterface() {};
        void ProcessUi();
        void DisplayFileInformation(const char* filename,uint32_t duration_ms);

        /// @brief Pops the oldest pending user-input command off the queue, if any.
        /// @param command Filled in with the popped command on success.
        /// @return true if a command was popped, false if the queue was empty.
        bool PopCommand(hw_interface::InputEvent &command);

        private:

    hw_interface::IInputHandler &input_handler_;
    hw_interface::IDisplay &display_;

    // Fixed-capacity ring buffer: avoids any dynamic allocation (std::queue) so this stays
    // usable on the RP2040 build, not just the native/Linux one.
    static constexpr size_t kCommandQueueCapacity = 16;
    hw_interface::InputEvent command_queue_[kCommandQueueCapacity]{};
    size_t queue_head_{0};
    size_t queue_tail_{0};
    size_t queue_count_{0};

    /// @brief Pushes a command onto the queue.
    /// @return true if the command was queued, false if the queue was full (command dropped).
    bool PushCommand(const hw_interface::InputEvent &command);

    };
} // namespace app::ui