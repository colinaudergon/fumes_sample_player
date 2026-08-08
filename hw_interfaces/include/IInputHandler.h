#pragma once
#include <cstdint>
namespace hw_interface
{

    enum class InputEventType
    {
        kNavigationEvent,
        kParameterChangeEvent,
        kSelectEvent
    };
    enum class ParameterChangeId
    {
        kPlaybackSpeedParameterId,
        kPlayParameterId,
        kStopParameterId,
        kFreezeParameterdId,
        kReverseParameterId,
        
    };

    enum class NavigationDirection
    {
        kUp,
        kDown
    };

    struct ParameterChange
    {
        ParameterChangeId id;
        float delta;
    };

    struct InputEvent
    {
        InputEventType type;
        union
        {
            NavigationDirection navigationDirection;
            ParameterChange parameter;
        };
    };

    class IInputHandler
    {

    public:
        virtual ~IInputHandler() = default;
        virtual int Init() = 0;
        virtual bool PollEvent(InputEvent &event) = 0;
    };
} // namespace hw_interface
