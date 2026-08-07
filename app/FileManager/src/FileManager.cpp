#include "FileManager.h"
#include <cstring>
#include <cstdio>

int app::filesystem::FileManager::Init(const char *disk_path, uint8_t supported_file_extensions_mask)
{
    if (disk_path == nullptr)
    {
        return kFileManagerNoDisk;
    }

    if (strnlen(disk_path, kDiskPathMaxLen + 1) != kDiskPathMaxLen)
    {
        return kFileManagerNoDisk;
    }

    memcpy(disk_path_, disk_path, kDiskPathMaxLen);

    int bank_count = CountBanksOnDisk();

    if (bank_count < 0)
    {
        return kFileManagerFail;
    }

    number_of_loaded_banks_ = bank_count;

    supported_file_extensions_mask_ = supported_file_extensions_mask;

    return kFileManagerOk;
}

size_t app::filesystem::FileManager::GetNumberOfBanks()
{
    return number_of_loaded_banks_;
}

int app::filesystem::FileManager::SelectBank(size_t bank_number)
{
    if (bank_number > number_of_loaded_banks_)
    {
        return kFileManagerInvalidBank;
    }

    // disk_path_ is not null-terminated, so print it with an explicit precision
    // rather than "%s".
    int written = snprintf(current_bank_path_, sizeof(current_bank_path_),
                           "%.*s/bank_%zu",
                           static_cast<int>(kDiskPathMaxLen), disk_path_, bank_number);

    if (written < 0 || static_cast<size_t>(written) >= sizeof(current_bank_path_))
    {
        return kFileManagerInvalidBank;
    }

    int number_of_file_in_bank = CountFilesOnCurrentBank();

    if (number_of_file_in_bank < 0)
    {
        return kFileManagerInvalidBank;
    }

    number_of_loaded_files_ = number_of_file_in_bank;
    std::printf("Bank path%s\n", current_bank_path_);
    current_bank_ = bank_number;
    return kFileManagerOk;
}

size_t app::filesystem::FileManager::GetNumberOfFileInCurrentBank()
{
    return number_of_loaded_files_;
}

int app::filesystem::FileManager::SelectFileByIndex(size_t file_index)
{
    if (file_index > number_of_loaded_files_)
    {
        return kFileManagerInvalidFile;
    }

    current_file_ = file_index;

    return 0;
}

int app::filesystem::FileManager::CountBanksOnDisk()
{
    if (disk_path_ == nullptr)
    {
        return kFileManagerNoDisk;
    }
    size_t valid_bank_count = 0;

    app::FsDir *dir = nullptr;
    app::FsResult open_dir_result = file_system_.OpenDir(&dir, disk_path_);
    if (open_dir_result != app::FsResult::kOk)
    {

        return kFileManagerFsError;
    }

    for (;;)
    {
        app::FsFileInfo info;
        app::FsResult read_dir_result = file_system_.ReadDir(dir, &info);

        if (read_dir_result != app::FsResult::kOk)
        {
            return kFileManagerFsError;
            break;
        }

        // FatFs signals end-of-directory with FR_OK and an empty name.
        if (info.name[0] == '\0')
        {
            break;
        }

        if ((info.attributes & app::FsAttribute::kDirectory) != 0)
        {
            size_t name_length = strnlen(info.name, sizeof(info.name));
            if (ValidateBankName(info.name, name_length) == kFileManagerOk)
            {
                valid_bank_count++;
            }
        }
    }
    file_system_.CloseDir(dir);
    return valid_bank_count;
}

int app::filesystem::FileManager::ValidateBankName(const char *folder_name, size_t length)
{
    constexpr char kPrefix[] = "bank_";
    constexpr size_t kPrefixLen = sizeof(kPrefix) - 1; // 5

    if (length > kFileNameMaxLength || length <= kPrefixLen)
    {
        return kFileManagerInvalidName;
    }

    for (size_t i = 0; i < kPrefixLen; i++)
    {
        // case-insensitive compare, ASCII only. Only fold case for letters -- kPrefix[i]
        // is always lowercase, but '_' isn't a letter and must be compared as-is, otherwise
        // (c | 0x20) turns '_' (0x5F) into 0x7F and the comparison always fails.
        char c = folder_name[i];
        if (c >= 'A' && c <= 'Z')
        {
            c |= 0x20;
        }
        if (c != kPrefix[i])
        {
            return kFileManagerInvalidName;
        }
    }

    for (size_t i = kPrefixLen; i < length; i++)
    {
        if (folder_name[i] < '0' || folder_name[i] > '9')
        {
            return kFileManagerInvalidName;
        }
    }

    return kFileManagerOk;
}

int app::filesystem::FileManager::CountFilesOnCurrentBank()
{
    if (current_bank_path_ == nullptr)
    {
        return kFileManagerInvalidBank;
    }

    size_t valid_file_count = 0;

    app::FsDir *dir = nullptr;
    app::FsResult open_dir_result = file_system_.OpenDir(&dir, current_bank_path_);
    if (open_dir_result != app::FsResult::kOk)
    {

        return kFileManagerFsError;
    }

    for (;;)
    {
        app::FsFileInfo info;
        app::FsResult read_dir_result = file_system_.ReadDir(dir, &info);

        if (read_dir_result != app::FsResult::kOk)
        {
            return kFileManagerFsError;
            break;
        }

        // FatFs signals end-of-directory with FR_OK and an empty name.
        if (info.name[0] == '\0')
        {
            break;
        }

        if ((info.attributes & app::FsAttribute::kDirectory) == 0)
        {
            size_t name_length = strnlen(info.name, sizeof(info.name));
            if (ValidateFileName(info.name, name_length) == kFileManagerOk)
            {
                valid_file_count++;
            }
        }
    }

    file_system_.CloseDir(dir);
    return valid_file_count;
}

int app::filesystem::FileManager::ValidateFileName(const char *filename, size_t length)
{
    if (filename == nullptr)
    {
        return kFileManagerInvalidName;
    }

    if (length == 0 || length > kFileNameMaxLength)
    {
        return kFileManagerInvalidName;
    }

    // invalid filename if there is more than one dot
    bool dot_located = false;
    size_t dot_location = 0;
    for (size_t i = 0; i < length; i++)
    {
        if (!IsCharacterValid(filename[i]))
        {
            return kFileManagerInvalidName;
        }

        if (filename[i] == '.')
        {
            if (!dot_located)
            {
                dot_located = true;
                dot_location = i;
            }
            else
            {
                return kFileManagerInvalidName;
            }
        }
    }

    // a dot must be present, must not be the first character (empty base name),
    // and must not be the last character (empty extension)
    if (!dot_located || dot_location == 0 || dot_location == length - 1)
    {
        return kFileManagerInvalidName;
    }

    // find extension: all the letters after the dot
    const char *extension = &filename[dot_location + 1];
    size_t extension_len = length - (dot_location + 1);

    if ((supported_file_extensions_mask_ & static_cast<uint8_t>(SupportedFileExtensions::KWav)) != 0)
    {
        constexpr char kWavExtension[] = "wav";
        constexpr size_t kWavExtensionLen = sizeof(kWavExtension) - 1;

        if (extension_len == kWavExtensionLen)
        {
            bool match = true;
            for (size_t i = 0; i < kWavExtensionLen; i++)
            {
                // case-insensitive compare, ASCII only
                if ((extension[i] | 0x20) != kWavExtension[i])
                {
                    match = false;
                    break;
                }
            }

            if (match)
            {
                return kFileManagerOk;
            }
        }
    }

    return kFileManagerInvalidName;
}

bool app::filesystem::FileManager::IsCharacterValid(char character)
{
    if ((character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') ||
        character == '_' ||
        character == '.')
    {
        return true;
    }

    return false;
}

bool app::filesystem::FileManager::CharacterMatchPattern(char character, const char *character_pattern, size_t character_pattern_len)
{
    if (character_pattern_len == 0)
    {
        return false;
    }

    constexpr char kCapitalOffset = 0x20;
    for (size_t i = 0; i < character_pattern_len; i++)
    {
        if (character == character_pattern[i])
        {
            return true;
        }
    }

    return false;
}
