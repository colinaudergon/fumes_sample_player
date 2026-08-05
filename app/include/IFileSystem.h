/**
 * @file IFileStytem.h
 * @brief File system interface placeholder.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace app
{
	enum class FsResult
	{
	kOk = 0,				/* (0) Succeeded */
	kDiskErr,			/* (1) A hard error occurred in the low level disk I/O layer */
	kIntErr,				/* (2) Assertion failed */
	kNotReady,			/* (3) The physical drive cannot work */
	kNoFile,				/* (4) Could not find the file */
	kNoPath,				/* (5) Could not find the path */
	kInvalidName,		/* (6) The path name format is invalid */
	kDenied,				/* (7) Access denied due to prohibited access or directory full */
	kExist,				/* (8) Access denied due to prohibited access */
	kInvalidObject,		/* (9) The file/directory object is invalid */
	kWriteProtected,		/* (10) The physical drive is write protected */
	kInvalidDrive,		/* (11) The logical drive number is invalid */
	kNotEnabled,			/* (12) The volume has no work area */
	kNoFilesystem,		/* (13) There is no valid FAT volume */
	kMkfsAborted,		/* (14) The f_mkfs() aborted due to any problem */
	kTimeout,				/* (15) Could not get a grant to access the volume within defined period */
	kLocked,				/* (16) The operation is rejected according to the file sharing policy */
	kNotEnoughCore,		/* (17) LFN working buffer could not be allocated */
	kTooManyOpenFiles,	/* (18) Number of open files > FF_FS_LOCK */
	kInvalidParameter	/* (19) Given parameter is invalid */
	};

	// Generic file attribute bits, independent of FatFs's AM_* constants.
	namespace FsAttribute
	{
		constexpr unsigned int kReadOnly = 0x01;
		constexpr unsigned int kHidden = 0x02;
		constexpr unsigned int kSystem = 0x04;
		constexpr unsigned int kDirectory = 0x10;
		constexpr unsigned int kArchive = 0x20;
	} // namespace FsAttribute

	// Generic file open mode/method bits for IFileSystem::Open()'s `mode` argument, independent
	// of FatFs's FA_* constants (though the concrete FatFsFileSystemAdapter currently maps them
	// 1:1 onto FA_* for simplicity -- a different IFileSystem implementation is free to
	// reinterpret these bits however it needs to, same as every other opaque config in this
	// codebase). Combine with bitwise OR, e.g. kWrite | kCreateAlways.
	namespace FsOpenMode
	{
		constexpr int kRead = 0x01;			// Open for reading; file must exist.
		constexpr int kWrite = 0x02;			// Open for writing.
		constexpr int kOpenExisting = 0x00;	// Open only an existing file (default if no other open method bit set).
		constexpr int kCreateNew = 0x04;		// Create a new file; fails if it already exists.
		constexpr int kCreateAlways = 0x08;	// Create a new file; overwrite/truncate if it already exists.
		constexpr int kOpenAlways = 0x10;		// Open existing file, or create it if it doesn't exist.
		constexpr int kOpenAppend = 0x30;		// Same as kOpenAlways, and seek to end of file after opening.
	} // namespace FsOpenMode

	// Opaque handles. Only the concrete adapter that created them (e.g. FatFsFileSystemAdapter)
	// knows their real layout; every other adapter (POSIX, RAM disk, mock, ...) is free to back
	// them with a completely different type. Callers only ever hold pointers to these.
	struct FsFile;
	struct FsDir;

	// Generic file/dir metadata, independent of any concrete filesystem's native info struct
	// (e.g. FatFs's FILINFO). Every adapter fills this in from its own representation.
	struct FsFileInfo
	{
		uint64_t size = 0;
		unsigned int attributes = 0; // bitmask of FsAttribute values
		int year = 0;
		int month = 0;	// 1-12
		int day = 0;	// 1-31
		int hour = 0;	// 0-23
		int minute = 0; // 0-59
		int second = 0; // 0-59
		char name[256] = {};
	};

	class IFileSystem
	{
	public:
		virtual ~IFileSystem() = default;

		virtual int Init() = 0;

		// Volume lifecycle
		virtual FsResult Mount(const char *path) = 0;
		virtual FsResult Unmount(const char *path) = 0;
		virtual FsResult Format(const char *path) = 0;
		virtual FsResult GetFreeSpace(const char *path, uint64_t *freeBytes, uint64_t *totalBytes) = 0;
		virtual FsResult GetLabel(const char *path, char *label, size_t labelLen, uint32_t *volumeSerial) = 0;
		virtual FsResult SetLabel(const char *path, const char *label) = 0;

		// File I/O
		// `mode` is a bitwise-OR of FsOpenMode values (e.g. FsOpenMode::kWrite | FsOpenMode::kCreateAlways).
		virtual FsResult Open(FsFile **file, const char *path, int mode) = 0;
		virtual FsResult Read(FsFile *file, void *buf, size_t len, size_t *bytesRead) = 0;
		virtual FsResult Write(FsFile *file, const void *buff, size_t btw, size_t *bytesWritten) = 0;
		virtual FsResult Close(FsFile *file) = 0;
		virtual FsResult Lseek(FsFile *file, size_t ofs) = 0;
		virtual FsResult Truncate(FsFile *file) = 0;
		virtual FsResult Sync(FsFile *file) = 0;
		virtual FsResult Expand(FsFile *file, uint64_t size, bool allocateNow) = 0;

		// Directory traversal
		virtual FsResult OpenDir(FsDir **dir, const char *path) = 0;
		virtual FsResult CloseDir(FsDir *dir) = 0;
		virtual FsResult ReadDir(FsDir *dir, FsFileInfo *info) = 0;
		virtual FsResult FindFirst(FsDir **dir, FsFileInfo *info, const char *path, const char *pattern) = 0;
		virtual FsResult FindNext(FsDir *dir, FsFileInfo *info) = 0;
		virtual FsResult MakeDir(const char *path) = 0;
		virtual FsResult ChangeDir(const char *path) = 0;
		virtual FsResult GetCwd(char *buff, size_t len) = 0;

		// Path / metadata management
		virtual FsResult Stat(const char *path, FsFileInfo *info) = 0;
		virtual FsResult Remove(const char *path) = 0;
		virtual FsResult Rename(const char *pathOld, const char *pathNew) = 0;
		virtual FsResult ChangeAttributes(const char *path, unsigned int attr, unsigned int mask) = 0;
		virtual FsResult SetTimestamp(const char *path, const FsFileInfo *info) = 0;

		// Deliberately not part of this interface (implemented in terms of the above, or belong to a
		// lower/global layer rather than a per-volume filesystem):
		//  - f_putc/f_puts/f_printf/f_gets: text convenience helpers, layer on top of Read/Write.
		//  - f_chdrive/f_fdisk/f_setcp:     process-wide drive/partition/codepage configuration.
	};

	// Factory returning the platform's singleton IFileSystem instance. Its concrete definition
	// lives in the FileSystem library (backed by FatFsFileSystemAdapter today) so that callers
	// which need to *own* an IFileSystem (e.g. AudioPlayer) can do so without depending on any
	// concrete filesystem implementation directly.
	IFileSystem &GetFileSystem();

} // namespace app
