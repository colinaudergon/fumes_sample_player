#include "FatFsFileSystemAdapter.h"

#include <cstdio>
#include <cstring>

namespace filesystem
{
    using app::FsDir;
    using app::FsFile;
    using app::FsFileInfo;
    using app::FsResult;

    namespace
    {
        // Concrete backing storage for the opaque FsFile/FsDir handles declared in
        // IFileSystem.h. Only this translation unit ever dereferences them; every caller
        // (and every other IFileSystem adapter) only ever sees app::FsFile*/app::FsDir*
        // pointers, so nothing outside this file depends on FatFs's FIL/DIR layout.
        struct FatFsFileHandle
        {
            FIL fil;
        };

        struct FatFsDirHandle
        {
            DIR dir;
        };

        FatFsFileHandle *ToNative(FsFile *file)
        {
            return reinterpret_cast<FatFsFileHandle *>(file);
        }

        FatFsDirHandle *ToNative(FsDir *dir)
        {
            return reinterpret_cast<FatFsDirHandle *>(dir);
        }

        FsFile *ToOpaque(FatFsFileHandle *file)
        {
            return reinterpret_cast<FsFile *>(file);
        }

        FsDir *ToOpaque(FatFsDirHandle *dir)
        {
            return reinterpret_cast<FsDir *>(dir);
        }

        void ToFsFileInfo(const FILINFO &native, FsFileInfo &info)
        {
            info.size = native.fsize;
            info.attributes = native.fattrib;
            info.year = ((native.fdate >> 9) & 0x7F) + 1980;
            info.month = (native.fdate >> 5) & 0x0F;
            info.day = native.fdate & 0x1F;
            info.hour = (native.ftime >> 11) & 0x1F;
            info.minute = (native.ftime >> 5) & 0x3F;
            info.second = (native.ftime & 0x1F) * 2;
            std::strncpy(info.name, native.fname, sizeof(info.name) - 1);
            info.name[sizeof(info.name) - 1] = '\0';
        }

        void ToNativeFileInfo(const FsFileInfo &info, FILINFO &native)
        {
            native.fdate = static_cast<WORD>(((info.year - 1980) << 9) | (info.month << 5) | info.day);
            native.ftime = static_cast<WORD>((info.hour << 11) | (info.minute << 5) | (info.second / 2));
        }
    } // namespace

    FsResult FatFsFileSystemAdapter::ToFsResult(FRESULT result)
    {
        switch (result)
        {
        case FR_OK:
            return FsResult::kOk;
        case FR_DISK_ERR:
            return FsResult::kDiskErr;
        case FR_INT_ERR:
            return FsResult::kIntErr;
        case FR_NOT_READY:
            return FsResult::kNotReady;
        case FR_NO_FILE:
            return FsResult::kNoFile;
        case FR_NO_PATH:
            return FsResult::kNoPath;
        case FR_INVALID_NAME:
            return FsResult::kInvalidName;
        case FR_DENIED:
            return FsResult::kDenied;
        case FR_EXIST:
            return FsResult::kExist;
        case FR_INVALID_OBJECT:
            return FsResult::kInvalidObject;
        case FR_WRITE_PROTECTED:
            return FsResult::kWriteProtected;
        case FR_INVALID_DRIVE:
            return FsResult::kInvalidDrive;
        case FR_NOT_ENABLED:
            return FsResult::kNotEnabled;
        case FR_NO_FILESYSTEM:
            return FsResult::kNoFilesystem;
        case FR_MKFS_ABORTED:
            return FsResult::kMkfsAborted;
        case FR_TIMEOUT:
            return FsResult::kTimeout;
        case FR_LOCKED:
            return FsResult::kLocked;
        case FR_NOT_ENOUGH_CORE:
            return FsResult::kNotEnoughCore;
        case FR_TOO_MANY_OPEN_FILES:
            return FsResult::kTooManyOpenFiles;
        case FR_INVALID_PARAMETER:
        default:
            return FsResult::kInvalidParameter;
        }
    }

    int FatFsFileSystemAdapter::Init()
    {
        // Nothing to do here: the block device backing FatFs is wired up separately via
        // filesystem::RegisterBlockDevice(); this adapter only talks to the FatFs API.
        return 0;
    }

    // --- Volume lifecycle -----------------------------------------------------

    FsResult FatFsFileSystemAdapter::Mount(const char *path)
    {
        return ToFsResult(f_mount(&fatfs_, path, 1));
    }

    FsResult FatFsFileSystemAdapter::Unmount(const char *path)
    {
        return ToFsResult(f_mount(nullptr, path, 0));
    }

    FsResult FatFsFileSystemAdapter::Format(const char *path)
    {
        BYTE workBuffer[FF_MAX_SS];
        return ToFsResult(f_mkfs(path, nullptr, workBuffer, sizeof(workBuffer)));
    }

    FsResult FatFsFileSystemAdapter::GetFreeSpace(const char *path, uint64_t *freeBytes, uint64_t *totalBytes)
    {
        DWORD freeClusters = 0;
        FATFS *fs = nullptr;
        FRESULT result = f_getfree(path, &freeClusters, &fs);
        if (result == FR_OK && fs != nullptr)
        {
            const uint64_t bytesPerCluster = static_cast<uint64_t>(fs->csize) * FF_MAX_SS;
            *freeBytes = static_cast<uint64_t>(freeClusters) * bytesPerCluster;
            *totalBytes = static_cast<uint64_t>(fs->n_fatent - 2) * bytesPerCluster;
        }
        return ToFsResult(result);
    }

    FsResult FatFsFileSystemAdapter::GetLabel(const char *path, char *label, size_t labelLen, uint32_t *volumeSerial)
    {
        char nativeLabel[FF_LFN_BUF + 1];
        DWORD serial = 0;
        FRESULT result = f_getlabel(path, nativeLabel, &serial);
        if (result == FR_OK)
        {
            if (label != nullptr && labelLen > 0)
            {
                std::strncpy(label, nativeLabel, labelLen - 1);
                label[labelLen - 1] = '\0';
            }
            if (volumeSerial != nullptr)
            {
                *volumeSerial = serial;
            }
        }
        return ToFsResult(result);
    }

    FsResult FatFsFileSystemAdapter::SetLabel(const char *path, const char *label)
    {
        // f_setlabel() takes a single string with an optional "<drive>:" prefix, so fold path
        // and label together the way FatFs expects (e.g. path="0:" label="AUDIO" -> "0:AUDIO").
        char combined[FF_LFN_BUF + 1];
        std::snprintf(combined, sizeof(combined), "%s%s", path, label);
        return ToFsResult(f_setlabel(combined));
    }

    // --- File I/O ---------------------------------------------------------------

    FsResult FatFsFileSystemAdapter::Open(FsFile **file, const char *path, int mode)
    {
        auto *native = new FatFsFileHandle();
        FRESULT result = f_open(&native->fil, path, static_cast<BYTE>(mode));
        if (result != FR_OK)
        {
            delete native;
            *file = nullptr;
            return ToFsResult(result);
        }
        *file = ToOpaque(native);
        return ToFsResult(result);
    }

    FsResult FatFsFileSystemAdapter::Read(FsFile *file, void *buf, size_t len, size_t *bytesRead)
    {
        UINT read = 0;
        FRESULT result = f_read(&ToNative(file)->fil, buf, static_cast<UINT>(len), &read);
        if (bytesRead != nullptr)
        {
            *bytesRead = read;
        }
        return ToFsResult(result);
    }

    FsResult FatFsFileSystemAdapter::Write(FsFile *file, const void *buff, size_t btw, size_t *bytesWritten)
    {
        UINT written = 0;
        FRESULT result = f_write(&ToNative(file)->fil, buff, static_cast<UINT>(btw), &written);
        if (bytesWritten != nullptr)
        {
            *bytesWritten = written;
        }
        return ToFsResult(result);
    }

    FsResult FatFsFileSystemAdapter::Close(FsFile *file)
    {
        FatFsFileHandle *native = ToNative(file);
        FRESULT result = f_close(&native->fil);
        delete native;
        return ToFsResult(result);
    }

    FsResult FatFsFileSystemAdapter::Lseek(FsFile *file, size_t ofs)
    {
        return ToFsResult(f_lseek(&ToNative(file)->fil, static_cast<FSIZE_t>(ofs)));
    }

    FsResult FatFsFileSystemAdapter::Truncate(FsFile *file)
    {
        return ToFsResult(f_truncate(&ToNative(file)->fil));
    }

    FsResult FatFsFileSystemAdapter::Sync(FsFile *file)
    {
        return ToFsResult(f_sync(&ToNative(file)->fil));
    }

    FsResult FatFsFileSystemAdapter::Expand(FsFile *file, uint64_t size, bool allocateNow)
    {
        return ToFsResult(f_expand(&ToNative(file)->fil, static_cast<FSIZE_t>(size), allocateNow ? 1 : 0));
    }

    // --- Directory traversal -----------------------------------------------------

    FsResult FatFsFileSystemAdapter::OpenDir(FsDir **dir, const char *path)
    {
        auto *native = new FatFsDirHandle();
        FRESULT result = f_opendir(&native->dir, path);
        if (result != FR_OK)
        {
            delete native;
            *dir = nullptr;
            return ToFsResult(result);
        }
        *dir = ToOpaque(native);
        return ToFsResult(result);
    }

    FsResult FatFsFileSystemAdapter::CloseDir(FsDir *dir)
    {
        FatFsDirHandle *native = ToNative(dir);
        FRESULT result = f_closedir(&native->dir);
        delete native;
        return ToFsResult(result);
    }

    FsResult FatFsFileSystemAdapter::ReadDir(FsDir *dir, FsFileInfo *info)
    {
        FILINFO native{};
        FRESULT result = f_readdir(&ToNative(dir)->dir, &native);
        if (result == FR_OK)
        {
            ToFsFileInfo(native, *info);
        }
        return ToFsResult(result);
    }

    FsResult FatFsFileSystemAdapter::FindFirst(FsDir **dir, FsFileInfo *info, const char *path, const char *pattern)
    {
        auto *native = new FatFsDirHandle();
        FILINFO nativeInfo{};
        FRESULT result = f_findfirst(&native->dir, &nativeInfo, path, pattern);
        if (result != FR_OK)
        {
            delete native;
            *dir = nullptr;
            return ToFsResult(result);
        }
        ToFsFileInfo(nativeInfo, *info);
        *dir = ToOpaque(native);
        return ToFsResult(result);
    }

    FsResult FatFsFileSystemAdapter::FindNext(FsDir *dir, FsFileInfo *info)
    {
        FILINFO nativeInfo{};
        FRESULT result = f_findnext(&ToNative(dir)->dir, &nativeInfo);
        if (result == FR_OK)
        {
            ToFsFileInfo(nativeInfo, *info);
        }
        return ToFsResult(result);
    }

    FsResult FatFsFileSystemAdapter::MakeDir(const char *path)
    {
        return ToFsResult(f_mkdir(path));
    }

    FsResult FatFsFileSystemAdapter::ChangeDir(const char *path)
    {
        return ToFsResult(f_chdir(path));
    }

    FsResult FatFsFileSystemAdapter::GetCwd(char *buff, size_t len)
    {
        return ToFsResult(f_getcwd(buff, static_cast<UINT>(len)));
    }

    // --- Path / metadata management -----------------------------------------------

    FsResult FatFsFileSystemAdapter::Stat(const char *path, FsFileInfo *info)
    {
        FILINFO native{};
        FRESULT result = f_stat(path, &native);
        if (result == FR_OK)
        {
            ToFsFileInfo(native, *info);
        }
        return ToFsResult(result);
    }

    FsResult FatFsFileSystemAdapter::Remove(const char *path)
    {
        return ToFsResult(f_unlink(path));
    }

    FsResult FatFsFileSystemAdapter::Rename(const char *pathOld, const char *pathNew)
    {
        return ToFsResult(f_rename(pathOld, pathNew));
    }

    FsResult FatFsFileSystemAdapter::ChangeAttributes(const char *path, unsigned int attr, unsigned int mask)
    {
        return ToFsResult(f_chmod(path, static_cast<BYTE>(attr), static_cast<BYTE>(mask)));
    }

    FsResult FatFsFileSystemAdapter::SetTimestamp(const char *path, const FsFileInfo *info)
    {
        FILINFO native{};
        ToNativeFileInfo(*info, native);
        return ToFsResult(f_utime(path, &native));
    }

} // namespace filesystem
