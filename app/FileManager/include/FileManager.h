
/**
 * @file FileManager.h
 * @brief
 */

#pragma once
#include "../../include/IFileSystem.h"

namespace app::filesystem
{
    enum class SupportedFileExtensions:uint8_t{
        kNone = 0x00,
        KWav = 0x01,
    };

    class FileManager
    {
    public:
        FileManager() : file_system_(GetFileSystem()) {};
        ~FileManager() {};
        int Init(const char* disk_path, uint8_t supported_file_extensions_mask);
        size_t GetNumberOfBanks();
        int SelectBank(size_t bank_number);
        size_t GetNumberOfFileInCurrentBank();
        int SelectFileByIndex(size_t file_index);
        int SelectNextFile();
        int SelectPreviousFile();
        /// @brief Returns the full path (bank path + filename) of the currently selected file.
        /// @return The full file path, or an empty string if no file is currently selected/valid.
        const char *GetSelectedFilePath();
        /// @brief Returns just the currently selected file's name (e.g. "kick.wav"), without the
        /// bank path prefix GetSelectedFilePath() includes.
        /// @return The file name, or an empty string if no file is currently selected/valid.
        const char *GetFileName();
        /// @brief Returns the index of the currently selected bank (see SelectBank()).
        size_t GetCurrentBankIndex();
        /// @brief Returns the index of the currently selected file within the current bank (see
        /// SelectFileByIndex()/SelectNextFile()/SelectPreviousFile()).
        size_t GetCurrentFileIndex();
    private:
        app::IFileSystem &file_system_;
        static constexpr size_t kNumMaxBank = 64;
        static constexpr size_t kFileNameMaxLength = 64;
        static constexpr size_t kDiskPathMaxLen = 2; // Number + column (ex: "0:")
        
        uint8_t supported_file_extensions_mask_{0x00};
        
        // +1 for the null terminator: disk_path_ is handed to IFileSystem::OpenDir() as a
        // plain C-string (see CountBanksOnDisk()), so it must be NUL-terminated, unlike
        // SelectBank()'s use of disk_path_ via "%.*s" (which relies on kDiskPathMaxLen instead).
        char disk_path_[kDiskPathMaxLen + 1];
        size_t current_bank_{0};
        // 2 for disk path, bank_[2]
        static constexpr size_t kBankPathMaxLen = 10;
        char current_bank_path_[kBankPathMaxLen];
        size_t number_of_loaded_banks_{0};

        size_t number_of_loaded_files_{0};
        size_t current_file_{0};

        // Bank path + '/' + filename + null terminator, matching AudioPlayer::kMaxFilePathLength
        // (see app/AudioPlayer/include/AudioPlayer.h) so GetSelectedFilePath()'s output can be
        // handed straight to AudioPlayer::LoadFile() without further length checks.
        static constexpr size_t kFullFilePathMaxLen = 256;
        char selected_file_path_[kFullFilePathMaxLen] = {};
        
        int CountBanksOnDisk();
        /// @brief Validates that the folder name is a valid bank name
        /// @param folder_name The name of the folder name to validate
        /// @param length 
        /// @return kFileManagerOk if valid, non-zero value otherwise
        int ValidateBankName(const char* folder_name, size_t length);
        int CountFilesOnCurrentBank();
        /// @brief Validates that the filename follows the correct pattern (any letter, any_number, valid extension)
        /// @param filename 
        /// @param length 
        /// @return kFileManagerOk if valid, non-zero value otherwise
        int ValidateFileName(const char* filename, size_t length);
        bool IsCharacterValid(char character);
        bool CharacterMatchPattern(char character, const char* character_pattern, size_t character_pattern_len);
        static constexpr int kFileManagerOk = 0;
        static constexpr int kFileManagerFail = -1;
        static constexpr int kFileManagerInvalidName = -2;
        static constexpr int kFileManagerNoDisk = -3;
        static constexpr int kFileManagerFsError = -4;
        static constexpr int kFileManagerInvalidBank = -5;
        static constexpr int kFileManagerInvalidFile = -6;

    };
} // namespace filesystem