# SdInterface: vendored carlk3/no-OS-FatFS-SD-SPI-RPi-Pico SD-over-SPI hardware driver, wrapped
# behind filesystem::IBlockDevice (see ../../include/IBlockDevice.h and SdBlockDevice.h/.cpp).
#
# Only the driver subset of the vendored repo is built here (sd_card.c, sd_spi.c, spi.c, crc.c,
# my_debug.c) -- its own bundled FatFs core, diskio glue (glue.c), and CLI/demo helpers
# (ff_stdio.c, f_util.c, rtc.c, demo_logging.c) are intentionally skipped: this project already
# has its own FatFs core (lib/FatFsCore), diskio glue (../../src/diskio.cpp), and IFileSystem
# adapter, so pulling those in as well would create a second, conflicting FatFs core in the build.
get_filename_component(SDSPI_VENDOR_ROOT
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../lib/no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI
        ABSOLUTE)

add_library(SdSpiDriver STATIC
        ${SDSPI_VENDOR_ROOT}/sd_driver/sd_card.c
        ${SDSPI_VENDOR_ROOT}/sd_driver/sd_spi.c
        ${SDSPI_VENDOR_ROOT}/sd_driver/spi.c
        ${SDSPI_VENDOR_ROOT}/sd_driver/crc.c
        ${SDSPI_VENDOR_ROOT}/src/my_debug.c
)

target_include_directories(SdSpiDriver PUBLIC
        ${SDSPI_VENDOR_ROOT}/sd_driver
        ${SDSPI_VENDOR_ROOT}/include
)

# Linking FatFsCore (rather than adding a hardcoded relative include path) gives sd_card.c/
# sd_spi.c their "ff.h" include (they only need FatFs's integer typedefs) via FatFsCore's own
# PUBLIC include directory, so there is exactly one FatFs core/headers set in the build.
target_link_libraries(SdSpiDriver PUBLIC
        FatFsCore
        hardware_spi
        hardware_dma
        pico_stdlib
)

add_library(SdInterface STATIC
        src/SdBlockDevice.cpp
        src/hw_config.c
)

target_include_directories(SdInterface PUBLIC
        include
)

target_link_libraries(SdInterface PUBLIC
        SdSpiDriver
        FileSystem
)
