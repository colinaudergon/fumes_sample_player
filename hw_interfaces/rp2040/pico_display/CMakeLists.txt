add_library(pico_display STATIC
        src/pico_display.cpp
        src/pico_display_u8x8_spi1.cpp
)

# Path is resolved relative to this file (not CMAKE_SOURCE_DIR) so this library builds
# identically regardless of which top-level CMakeLists.txt (native or platform/<board>) is
# configuring the project.
target_include_directories(pico_display PUBLIC
        include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(pico_display PUBLIC
        pico_stdlib
        hardware_spi
        hardware_gpio
        u8g2
)
