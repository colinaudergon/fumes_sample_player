/*
 * @file wav_format.h
 * @brief file with wav header format definition
 */

#pragma once

#include <cstdint>

typedef struct
{
    char blockID[4];         // 4 bytes
    uint32_t totalSize;      // 4 bytes
    char typeHeader[4];      // 4 bytes
    char fmt[4];             // 4 bytes
    uint32_t headerLen;      // 4bytes
    uint16_t typeOfFormat;   // 2 bytes
    uint16_t numbeOfChannel; // 2bytes
    uint32_t sampleRate;     // 4 bytes
    uint32_t byteRate;       // 4 bytes
    uint16_t blockAlign;     // 2 bytes
    uint16_t bitsPerSample;  // 2 bytes
    char dataHeader[4];      // 4 bytes
    uint32_t dataSize;       // 4 bytes
} wav_file_header_t;        // 44 bytes total
