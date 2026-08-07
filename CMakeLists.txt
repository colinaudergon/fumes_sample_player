# Native/Linux (host) build entry point.
#
# The Pico/RP2040 firmware build has its own, separate entry point in platform/rp2040
# (configure it with `cmake -S platform/rp2040 -B build-rp2040`) because it must locate and
# initialise the Pico SDK toolchain before the first project() call, which cannot coexist with
# a native host build in the same CMake configure run.
#
# This file builds the shared, hardware-agnostic app layer (AudioPlayer, FileSystem, FatFsCore)
# against native/Linux implementations of the hw_interfaces (hw_interfaces/linux), so it can be
# built and run directly on the host (e.g. WSL):
#   cmake -S . -B build && cmake --build build
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(sample_player C CXX)

add_subdirectory(lib/FatFsCore)
add_subdirectory(app/AudioPlayer)
add_subdirectory(app/FileSystem)
add_subdirectory(app/FileManager)
add_subdirectory(app/UserInterface)

add_subdirectory(hw_interfaces/linux)

add_executable(sample_player platform/linux/main.cpp)

# FatFsCore (ff.c) calls disk_read/disk_write/disk_ioctl/... which are only defined in
# FileSystem's diskio.cpp, while FileSystem links FatFsCore for its own headers/types -- i.e.
# these two static libraries have a circular symbol dependency. GNU ld resolves archives in a
# single left-to-right pass by default, so whichever of the two is listed first would leave the
# other's symbols unresolved. Wrapping them in --start-group/--end-group makes the linker keep
# re-scanning the group until every symbol is resolved, regardless of ordering.
target_link_libraries(sample_player PRIVATE
    -Wl,--start-group
    AudioPlayer
    FileSystem
    FileManager
    UserInterface
    null_audio_codec
    Display
    posix_block_device
    FatFsCore
    ConsoleInputHandler
    -Wl,--end-group
)

# Host-side CLI utility: packs a folder's contents into a FAT disk-image file consumable by
# wav_file_reader_native (see platform/linux/tools/make_disk_image.cpp). Only needs FileSystem
# and posix_block_device (no audio codec/AudioPlayer involved), but shares the same circular
# FatFsCore/FileSystem dependency as the main executable above.
add_executable(make_disk_image platform/linux/tools/make_disk_image.cpp)

target_link_libraries(make_disk_image PRIVATE
    -Wl,--start-group
    FileSystem
    posix_block_device
    FatFsCore
    -Wl,--end-group
)
