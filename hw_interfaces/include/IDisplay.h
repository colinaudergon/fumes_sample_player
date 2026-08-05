/**
 * @file IDisplay.h
 * @brief Display interface.
 */

 #pragma once


namespace hw_interface
{
    class IDisplay
    {
    public:
        virtual ~IDisplay() = default;
        virtual int Init() = 0;
    };
}// namespace hw_interface