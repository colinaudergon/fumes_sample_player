add_library(null_audio_codec STATIC
        src/NullAudioCodec.cpp
)

# Path is resolved relative to this file (not CMAKE_SOURCE_DIR) so this library builds
# identically regardless of which top-level CMakeLists.txt (native or platform/<board>) is
# configuring the project.
target_include_directories(null_audio_codec PUBLIC
        include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

# miniaudio dlopen's the actual backend (ALSA/PulseAudio/...) at runtime, but still needs these
# libc/pthread symbols linked in directly.
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(null_audio_codec PUBLIC Threads::Threads dl m)
endif()
