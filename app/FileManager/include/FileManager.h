
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
        
    private:
        app::IFileSystem &file_system_;
        static constexpr size_t kNumMaxBank = 64;
        static constexpr size_t kFileNameMaxLength = 64;
        static constexpr size_t kDiskPathMaxLen = 2; // Number + column (ex: "0:")
        
        uint8_t supported_file_extensions_mask_{0x00};
        
        char disk_path_[kDiskPathMaxLen];
        size_t current_bank_{0};
        // 2 for disk path, bank_[2]
        static constexpr size_t kBankPathMaxLen = 10;
        char current_bank_path_[kBankPathMaxLen];
        size_t number_of_loaded_banks_{0};

        size_t number_of_loaded_files_{0};
        size_t current_file_{0};
        
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