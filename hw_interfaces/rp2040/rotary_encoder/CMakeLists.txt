add_library(rotary_encoder STATIC
        src/pico_rotary_encoder_input_handler.cpp
)

# Path is resolved relative to this file (not CMAKE_SOURCE_DIR) so this library builds
# identically regardless of which top-level CMakeLists.txt (native or platform/<board>) is
# configuring the project.
target_include_directories(rotary_encoder PUBLIC
        include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(rotary_encoder PUBLIC
        pico_stdlib
        hardware_gpio
        hardware_sync
        gpio_interrupts
)
