#pragma once

#include "../../../include/IAudioCodec.h"

namespace hw_interface
{
    class PicoAudioCodec : public IAudioCodec
    {
    public:
        PicoAudioCodec() = default;
        ~PicoAudioCodec() override = default;

        int Init() override;
        int RegisterFillCallback(buffer_fill_cb cb) override;

    private:
        buffer_fill_cb fill_cb_ = nullptr;
    };
}