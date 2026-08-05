#pragma once
#include <vector>
#include "IInputHandler.h"
namespace hw_interface
{
    /// @brief Aggregates multiple IInputHandler sources (e.g. buttons, ADC, console) behind a
    /// single IInputHandler so consumers (UserInterface) don't need to know how many physical
    /// sources exist or what kind they are.
    class CompositeInputHandler : public IInputHandler
    {
    public:
        CompositeInputHandler() = default;
        ~CompositeInputHandler() override = default;
        /// @brief Registers a source handler. Ownership stays with the caller; this class only
        /// stores a non-owning pointer, mirroring IInputHandler/IDisplay usage elsewhere.
        void AddSource(IInputHandler *source)
        {
            if (source != nullptr)
            {
                sources_.push_back(source);
            }
        }
        int Init() override
        {
            for (IInputHandler *source : sources_)
            {
                int result = source->Init();
                if (result != 0)
                {
                    return result;
                }
            }
            return 0;
        }
        /// @brief Polls each source in registration order and returns the first event found.
        /// Sources are expected to be non-blocking (return false immediately if idle).
        bool PollEvent(InputEvent &out) override
        {
            for (IInputHandler *source : sources_)
            {
                if (source->PollEvent(out))
                {
                    return true;
                }
            }
            return false;
        }
    private:
        std::vector<IInputHandler *> sources_;
    };
} // namespace hw_interface