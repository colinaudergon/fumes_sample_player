/**
 * @file FileSystemFactory.cpp
 * @brief Provides the singleton IFileSystem instance returned by app::GetFileSystem().
 *
 * Kept in its own translation unit so callers that only need the abstract factory (e.g.
 * AudioPlayer) never have to include/link against the concrete FatFsFileSystemAdapter directly.
 */

#include "FatFsFileSystemAdapter.h"
#include "IFileSystem.h"

namespace app
{
    IFileSystem &GetFileSystem()
    {
        static filesystem::FatFsFileSystemAdapter instance;
        return instance;
    }
} // namespace app
