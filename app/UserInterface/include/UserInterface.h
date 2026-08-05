/**
 * @file UserInterface.h
 * @brief
 */

#pragma once

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
    private:
    hw_interface::IInputHandler &input_handler_;
    hw_interface::IDisplay &display_;

    };
} // namespace app::ui