#include "UserInterface.h"

void app::ui::UserInterface::ProcessUi()
{
    hw_interface::InputEvent event;
    if (input_handler_.PollEvent(event))
    {
        if (event.type == hw_interface::InputEventType::kNavigationEvent)
        {
            if (event.navigationDirection == hw_interface::NavigationDirection::kUp)
            {
                display_.ShowText("Up cmd\n");
            }
            else if (event.navigationDirection == hw_interface::NavigationDirection::kDown)
            {
                display_.ShowText("Down cmd\n");
            }
        }
    }
}