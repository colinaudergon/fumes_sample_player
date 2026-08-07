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
        else if (event.type == hw_interface::InputEventType::kParameterChangeEvent)
        {
            if (event.parameter.id == hw_interface::ParameterChangeId::kPlaybackSpeedParameterId)
            {
                display_.ShowText("Play back speed\n");
            }
            if (event.parameter.id == hw_interface::ParameterChangeId::kPlayParameterId)
            {
                display_.ShowText("Play\n");
            }
            if (event.parameter.id == hw_interface::ParameterChangeId::kStopParameterId)
            {
                display_.ShowText("Stop\n");
            }
        }

        // Queue the raw event so the main loop can act on it (e.g. drive AudioPlayer/
        // FileManager) without UserInterface needing to know about those app-layer types.
        PushCommand(event);
    }
}

void app::ui::UserInterface::DisplayFileInformation(const char *filename, uint32_t duration_ms)
{
        if(filename == nullptr)
    {
        display_.ShowText("file: unknown\n");
        return;
    }
    display_.DisplayFileInfo(filename,duration_ms);
}

bool app::ui::UserInterface::PushCommand(const hw_interface::InputEvent &command)
{
    if (queue_count_ >= kCommandQueueCapacity)
    {
        // Queue full: drop the incoming command rather than overwrite an older, unprocessed one.
        return false;
    }

    command_queue_[queue_tail_] = command;
    queue_tail_ = (queue_tail_ + 1) % kCommandQueueCapacity;
    queue_count_++;

    return true;
}

bool app::ui::UserInterface::PopCommand(hw_interface::InputEvent &command)
{
    if (queue_count_ == 0)
    {
        return false;
    }

    command = command_queue_[queue_head_];
    queue_head_ = (queue_head_ + 1) % kCommandQueueCapacity;
    queue_count_--;

    return true;
}

