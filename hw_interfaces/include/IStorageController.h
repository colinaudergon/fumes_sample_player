/**
 * @file IStorageController.h
 * @brief Storage controller interface.
 */

#pragma once

namespace hw_interface
{

    class IStorageController
    {
    public:
        virtual ~IStorageController() = default;

        virtual int Init() = 0;
    };

} // namespace hw_interface
