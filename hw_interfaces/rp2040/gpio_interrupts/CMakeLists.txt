add_library(gpio_interrupts STATIC
        src/gpio_interrupt_dispatcher.cpp
)

target_include_directories(gpio_interrupts PUBLIC
        include
)

target_link_libraries(gpio_interrupts PUBLIC
        pico_stdlib
        hardware_gpio
)
