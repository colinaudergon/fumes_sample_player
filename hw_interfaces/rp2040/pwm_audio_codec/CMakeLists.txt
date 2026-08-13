add_library(pwm_audio_codec STATIC
        src/pwm_audio_codec.cpp
)

# Path is resolved relative to this file (not CMAKE_SOURCE_DIR) so this library builds
# identically regardless of which top-level CMakeLists.txt (native or platform/<board>) is
# configuring the project.
target_include_directories(pwm_audio_codec PUBLIC
        include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(pwm_audio_codec PUBLIC
        pico_stdlib
        hardware_dma
        hardware_pwm
        hardware_irq
        hardware_clocks
        hardware_structs
)

# TEMPORARY: enable to print periodic diagnostics from RefillNextBuffer() (refill count, and
# min/max of the left channel's PWM buffer) over stdio -- useful for confirming whether real,
# non-flat audio data is actually reaching the PWM output buffers. Remove once the "no audio
# output" issue is resolved.
option(PWM_AUDIO_CODEC_DEBUG_PRINT "Enable pwm_audio_codec diagnostic printf output" OFF)
if(PWM_AUDIO_CODEC_DEBUG_PRINT)
        target_compile_definitions(pwm_audio_codec PRIVATE PWM_AUDIO_CODEC_DEBUG_PRINT)
endif()

