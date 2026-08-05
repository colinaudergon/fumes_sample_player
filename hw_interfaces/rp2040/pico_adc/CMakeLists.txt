add_library(pico_adc STATIC
        src/pico_adc.cpp
)

target_include_directories(pico_adc PUBLIC
        include
)

target_link_libraries(pico_adc PUBLIC
        pico_stdlib
        hardware_adc
)