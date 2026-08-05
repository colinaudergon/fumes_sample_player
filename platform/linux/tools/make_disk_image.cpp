/**
 * @file make_disk_image.cpp
 * @brief Host-side CLI utility: packs a folder's contents into a FAT disk-image file that
 * platform/linux/main.cpp (or any native/Linux run of wav_file_reader) can mount as "0:".
 *
 * This goes through the exact same hardware-agnostic stack the app itself uses --
 * PosixBlockDevice (IBlockDevice) + FatFsFileSystemAdapter (IFileSystem) -- so the resulting
 * image is guaranteed to be readable by wav_file_reader_native without any format mismatch.
 *
 * Usage:
 *   make_disk_image <source_folder> <output_image.img> [sector_count]
 *
 * sector_count defaults to 65536 (32 MiB at the default 512-byte sectors); pass a larger value
 * if the source folder doesn't fit.
 *
 * Note: FatFs is configured with FF_USE_LFN == 0 (see lib/FatFsCore/ff15/source/ffconf.h), so
 * only 8.3 short filenames are supported -- files/folders with longer names will fail to copy.
 */

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "FatFsFileSystemAdapter.h"
#include "IBlockDevice.h"
#include "PosixBlockDevice.h"

namespace fs = std::filesystem;

namespace
{
    // Converts a path relative to the source root (e.g. "sub/dir/file.wav") into the FAT path
    // FatFs expects (e.g. "0:/sub/dir/file.wav"), normalizing host path separators to '/'.
    std::string ToFatPath(const fs::path &relative_path)
    {
        std::string fat_path = "0:/";
        fat_path += relative_path.generic_string();
        return fat_path;
    }

    bool CopyFileToImage(app::IFileSystem &file_system, const fs::path &host_file_path,
                          const std::string &fat_path)
    {
        std::FILE *source = std::fopen(host_file_path.string().c_str(), "rb");
        if (source == nullptr)
        {
            std::fprintf(stderr, "Failed to open host file: %s\n", host_file_path.string().c_str());
            return false;
        }

        app::FsFile *dest = nullptr;
        app::FsResult open_result = file_system.Open(
            &dest, fat_path.c_str(), app::FsOpenMode::kWrite | app::FsOpenMode::kCreateAlways);
        if (open_result != app::FsResult::kOk)
        {
            std::fprintf(stderr, "Failed to create %s in image (FsResult=%d)\n", fat_path.c_str(),
                         static_cast<int>(open_result));
            std::fclose(source);
            return false;
        }

        std::vector<uint8_t> buffer(64 * 1024);
        bool ok = true;
        size_t read_bytes = 0;
        while ((read_bytes = std::fread(buffer.data(), 1, buffer.size(), source)) > 0)
        {
            size_t written_bytes = 0;
            app::FsResult write_result = file_system.Write(dest, buffer.data(), read_bytes, &written_bytes);
            if (write_result != app::FsResult::kOk || written_bytes != read_bytes)
            {
                std::fprintf(stderr, "Failed to write %s to image (FsResult=%d)\n", fat_path.c_str(),
                             static_cast<int>(write_result));
                ok = false;
                break;
            }
        }

        file_system.Close(dest);
        std::fclose(source);
        return ok;
    }
} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "Usage: %s <source_folder> <output_image.img> [sector_count]\n", argv[0]);
        return 1;
    }

    const fs::path source_folder = argv[1];
    const std::string image_path = argv[2];
    const uint32_t sector_count = (argc >= 4) ? static_cast<uint32_t>(std::stoul(argv[3])) : 65536;

    if (!fs::is_directory(source_folder))
    {
        std::fprintf(stderr, "Source folder does not exist or is not a directory: %s\n",
                     source_folder.string().c_str());
        return 1;
    }

    // Start from a fresh image every run so stale files/deletions on the host are reflected.
    std::error_code remove_error;
    fs::remove(image_path, remove_error);

    hw_interface::PosixBlockDevice block_device(image_path, 512, sector_count);
    filesystem::RegisterBlockDevice(0, &block_device);

    filesystem::FatFsFileSystemAdapter file_system;
    file_system.Init();

    app::FsResult format_result = file_system.Format("0:");
    if (format_result != app::FsResult::kOk)
    {
        std::fprintf(stderr, "Failed to format %s (FsResult=%d)\n", image_path.c_str(),
                     static_cast<int>(format_result));
        return 1;
    }

    app::FsResult mount_result = file_system.Mount("0:");
    if (mount_result != app::FsResult::kOk)
    {
        std::fprintf(stderr, "Failed to mount %s (FsResult=%d)\n", image_path.c_str(),
                     static_cast<int>(mount_result));
        return 1;
    }

    int copied_files = 0;
    int failed_entries = 0;

    // recursive_directory_iterator visits a directory entry itself before the entries it
    // contains, so creating directories as we encounter them is enough to have parents ready
    // before their children.
    for (const fs::directory_entry &entry : fs::recursive_directory_iterator(source_folder))
    {
        const fs::path relative_path = fs::relative(entry.path(), source_folder);
        const std::string fat_path = ToFatPath(relative_path);

        if (entry.is_directory())
        {
            app::FsResult mkdir_result = file_system.MakeDir(fat_path.c_str());
            if (mkdir_result != app::FsResult::kOk && mkdir_result != app::FsResult::kExist)
            {
                std::fprintf(stderr, "Failed to create directory %s (FsResult=%d)\n", fat_path.c_str(),
                             static_cast<int>(mkdir_result));
                ++failed_entries;
            }
            continue;
        }

        if (entry.is_regular_file())
        {
            if (CopyFileToImage(file_system, entry.path(), fat_path))
            {
                ++copied_files;
            }
            else
            {
                ++failed_entries;
            }
            continue;
        }

        std::fprintf(stderr, "Skipping unsupported entry: %s\n", entry.path().string().c_str());
    }

    file_system.Unmount("0:");

    std::printf("Copied %d file(s) into %s", copied_files, image_path.c_str());
    if (failed_entries > 0)
    {
        std::printf(" (%d entrie(s) failed)", failed_entries);
    }
    std::printf("\n");

    return (failed_entries > 0) ? 1 : 0;
}
