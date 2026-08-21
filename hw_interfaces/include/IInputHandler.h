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
        kStartMarkerParameterId,
        kStopMarkerParameterId,
        kLoopParameterId,
        kGlitchParameterId,
        // ---- Individual GlitchEngine parameter controls (see GlitchEngine.h), each mapped
        // 1:1 to one of its setters rather than derived from the single kGlitchAmountParameterId.
        kNoiseOutputParameterId,
        kPitchModParameterId,
        kBitcrushEnableParameterId,
        kPitchModProbabilityParameterId,
        kStutterProbabilityParameterId,
        kSampleRateReductionParameterId,
        kReductionFactorParameterId,
        // ---- Click generator controls (see GlitchEngine::ClickGenerator).
        kClickOutputParameterId,
        kClickDensityParameterId,

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
