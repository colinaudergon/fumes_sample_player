# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(wav_file_reader C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# This CMakeLists.txt lives in platform/rp2040; shared, hardware-agnostic components live at
# the repository root, so they are pulled in via an explicit out-of-tree source/binary pair.
get_filename_component(WAV_FILE_READER_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)

add_subdirectory(${WAV_FILE_READER_ROOT}/lib/FatFsCore ${CMAKE_CURRENT_BINARY_DIR}/lib/FatFsCore)
add_subdirectory(${WAV_FILE_READER_ROOT}/app/AudioPlayer ${CMAKE_CURRENT_BINARY_DIR}/app/AudioPlayer)
add_subdirectory(${WAV_FILE_READER_ROOT}/app/FileSystem ${CMAKE_CURRENT_BINARY_DIR}/app/FileSystem)
add_subdirectory(${WAV_FILE_READER_ROOT}/app/FileManager ${CMAKE_CURRENT_BINARY_DIR}/app/FileManager)
add_subdirectory(${WAV_FILE_READER_ROOT}/app/AudioEffects ${CMAKE_CURRENT_BINARY_DIR}/app/AudioEffects)
add_subdirectory(${WAV_FILE_READER_ROOT}/app/UserInterface ${CMAKE_CURRENT_BINARY_DIR}/app/UserInterface)

# AudioPlayer's per-callback scratch buffers default to a size matching the native/Linux build's
# NullAudioCodec (4096 frames), which would reserve ~128KB of SRAM it's never asked to use here
# -- hw_interface::PicoAudioCodec only ever calls back with 256-frame buffers (see
# PicoAudioCodec::kBufferSize). Overriding this down to 256 for just this build tree is what
# keeps wav_file_reader.elf's .bss inside the RP2040's much smaller SRAM.
target_compile_definitions(AudioPlayer PUBLIC APP_AUDIO_MAX_FRAMES_PER_CALLBACK=256)
# SD-over-SPI IBlockDevice implementation (wraps the vendored carlk3/no-OS-FatFS-SD-SPI-RPi-Pico
# driver, see app/FileSystem/hw_layer/SdInterface/CMakeLists.txt); RP2040-only, since it depends
# on Pico SDK hardware_spi/hardware_dma.
add_subdirectory(${WAV_FILE_READER_ROOT}/app/FileSystem/hw_layer/SdInterface ${CMAKE_CURRENT_BINARY_DIR}/app/FileSystem/hw_layer/SdInterface)

# RP2040-only hardware interfaces (ADC and PWM audio output codec), depend on Pico SDK
# hardware_adc/hardware_dma/hardware_pwm.
add_subdirectory(${WAV_FILE_READER_ROOT}/hw_interfaces/rp2040/pico_adc ${CMAKE_CURRENT_BINARY_DIR}/hw_interfaces/rp2040/pico_adc)
add_subdirectory(${WAV_FILE_READER_ROOT}/hw_interfaces/rp2040/pwm_audio_codec ${CMAKE_CURRENT_BINARY_DIR}/hw_interfaces/rp2040/pwm_audio_codec)

# RP2040-only user-input sources (ported from gv_lib's pico_gv_drivers): gpio_interrupts is a
# shared single-callback GPIO IRQ dispatcher, rotary_encoder is a hw_interface::IInputHandler
# implementation built on top of it.
add_subdirectory(${WAV_FILE_READER_ROOT}/hw_interfaces/rp2040/gpio_interrupts ${CMAKE_CURRENT_BINARY_DIR}/hw_interfaces/rp2040/gpio_interrupts)
add_subdirectory(${WAV_FILE_READER_ROOT}/hw_interfaces/rp2040/rotary_encoder ${CMAKE_CURRENT_BINARY_DIR}/hw_interfaces/rp2040/rotary_encoder)

# u8g2 (vendored as a git submodule at lib/u8g2) drives PicoDisplay's SSD1306 SPI OLED; RP2040-
# only, depends on Pico SDK hardware_spi/hardware_gpio.
add_subdirectory(${WAV_FILE_READER_ROOT}/lib/u8g2 ${CMAKE_CURRENT_BINARY_DIR}/lib/u8g2)

# u8g2's vendored CMakeLists.txt doesn't link pico_stdlib, so it never inherits the Pico SDK's
# global -ffunction-sections/-fdata-sections compile options (set via pico_standard_link's
# INTERFACE target, which is only pulled in by libraries that link pico_stdlib). Without them,
# --gc-sections has nothing to strip: u8g2_fonts.c bundles EVERY built-in font (hundreds) as one
# translation unit, so linking in a single font (e.g. u8g2_font_6x13_tr, used by PicoDisplay)
# would otherwise pull in the whole ~14MB file and blow the RP2040's 2MB flash budget.
target_compile_options(u8g2 PRIVATE -ffunction-sections -fdata-sections)

add_subdirectory(${WAV_FILE_READER_ROOT}/hw_interfaces/rp2040/pico_display ${CMAKE_CURRENT_BINARY_DIR}/hw_interfaces/rp2040/pico_display)

# Add executable. Default name is the project name, version 0.1

add_executable(wav_file_reader wav_file_reader.cpp utils/cross_core_queues.cpp core1/core1_main.cpp)

pico_set_program_name(wav_file_reader "wav_file_reader")
pico_set_program_version(wav_file_reader "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(wav_file_reader 1)
pico_enable_stdio_usb(wav_file_reader 1)

# Add the standard library to the build. pico_stdlib is linked here (rather than inside the
# shared libraries) so that AudioPlayer/FileSystem stay free of any Pico SDK dependency and can
# also be built for the native/Linux platform (see the repository root CMakeLists.txt).
#
# NOTE: FatFsCore's ff.c calls disk_read/disk_write/get_fattime (defined in FileSystem's
# diskio.cpp), while FileSystem's FatFsFileSystemAdapter.cpp calls f_mount/f_open/etc. (defined
# in FatFsCore) -- i.e. FatFsCore and FileSystem are mutually dependent static libraries. Once
# FileManager/SdInterface were added to this link line, the flattened archive order left one of
# these two symbol sets unresolved. Wrapping them (and their dependents) in a linker group makes
# ld re-scan the group until all references resolve, regardless of archive order.
target_link_libraries(wav_file_reader
    pico_stdlib
    pico_multicore
    hardware_clocks
    -Wl,--start-group
    FileSystem
    FileManager
    SdInterface
    SdSpiDriver
    FatFsCore
    pico_adc
    pwm_audio_codec
    rotary_encoder
    gpio_interrupts
    pico_display
    u8g2
    UserInterface
    AudioPlayer
    AudioEffects
    -Wl,--end-group)

# Add the standard include files to the build
target_include_directories(wav_file_reader PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(wav_file_reader)

# Report FLASH/RAM usage (and whether it fits within the RP2040's memory regions, as defined by
# the Pico SDK linker script) at the end of every link. -Wl,--print-memory-usage is a standard
# GNU ld feature; if the binary doesn't actually fit, the linker already fails the build outright,
# so this is purely an informational summary for every successful build.
target_link_options(wav_file_reader PRIVATE -Wl,--print-memory-usage)

