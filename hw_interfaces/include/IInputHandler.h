#pragma once
#include <cstdint>
namespace hw_interface
{
    typedef uint8_t parameter_id_t;

    enum class InputEventType
    {
        kNavigationEvent,
        kParameterChangeEvent,
        kSelectEvent
    };
    enum class NavigationDirection
    {
        kUp,
        kDown
    };

    struct ParameterChange
    {
        parameter_id_t id;
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
