/**
 * @file FatFsFileSystemAdapter.h
 * @brief Fat Fs File System adapter
 */

#pragma once

#include "../../include/IFileSystem.h"
#include "ff.h"

namespace filesystem
{
    class FatFsFileSystemAdapter : public app::IFileSystem
    {
    public:
        int Init() override;
        
        // Volume lifecycle
        app::FsResult Mount(const char *path) override;
        app::FsResult Unmount(const char *path) override;
        app::FsResult Format(const char *path) override;
        app::FsResult GetFreeSpace(const char *path, uint64_t *freeBytes, uint64_t *totalBytes) override;
        app::FsResult GetLabel(const char *path, char *label, size_t labelLen, uint32_t *volumeSerial) override;
        app::FsResult SetLabel(const char *path, const char *label) override;

        // File I/O
        app::FsResult Open(app::FsFile **file, const char *path, int mode) override;
        app::FsResult Read(app::FsFile *file, void *buf, size_t len, size_t *bytesRead) override;
        app::FsResult Write(app::FsFile *file, const void *buff, size_t btw, size_t *bytesWritten) override;
        app::FsResult Close(app::FsFile *file) override;
        app::FsResult Lseek(app::FsFile *file, size_t ofs) override;
        app::FsResult Truncate(app::FsFile *file) override;
        app::FsResult Sync(app::FsFile *file) override;
        app::FsResult Expand(app::FsFile *file, uint64_t size, bool allocateNow) override;

        // Directory traversal
        app::FsResult OpenDir(app::FsDir **dir, const char *path) override;
        app::FsResult CloseDir(app::FsDir *dir) override;
        app::FsResult ReadDir(app::FsDir *dir, app::FsFileInfo *info) override;
        app::FsResult FindFirst(app::FsDir **dir, app::FsFileInfo *info, const char *path, const char *pattern) override;
        app::FsResult FindNext(app::FsDir *dir, app::FsFileInfo *info) override;
        app::FsResult MakeDir(const char *path) override;
        app::FsResult ChangeDir(const char *path) override;
        app::FsResult GetCwd(char *buff, size_t len) override;

        // Path / metadata management
        app::FsResult Stat(const char *path, app::FsFileInfo *info) override;
        app::FsResult Remove(const char *path) override;
        app::FsResult Rename(const char *pathOld, const char *pathNew) override;
        app::FsResult ChangeAttributes(const char *path, unsigned int attr, unsigned int mask) override;
        app::FsResult SetTimestamp(const char *path, const app::FsFileInfo *info) override;

        private:

        app::FsResult ToFsResult(FRESULT result);

        FATFS fatfs_{}; // backing storage for f_mount(); must outlive the mounted volume.
    };
} // namespace filesystem
