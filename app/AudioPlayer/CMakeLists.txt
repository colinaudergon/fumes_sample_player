add_library(AudioPlayer STATIC
        src/AudioPlayer.cpp
        audio/wav/wav_file_handler.cpp
)

# Paths are resolved relative to this file (not CMAKE_SOURCE_DIR) so this library builds
# identically whether it's pulled in by the root (native) CMakeLists.txt or by a
# platform/<board> one, e.g. platform/rp2040/CMakeLists.txt.
target_include_directories(AudioPlayer PUBLIC
        include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../hw_interfaces/include
        ${CMAKE_CURRENT_SOURCE_DIR}/audio/wav
)

# AudioPlayer only depends on hardware-agnostic interfaces (IAudioCodec, IFileSystem) at the
# API level, but it now owns its IFileSystem via app::GetFileSystem() (see IFileSystem.h), so
# it needs the FileSystem library at link time to resolve that factory symbol. FileSystem's own
# concrete IBlockDevice implementation (and any platform-specific dependency such as
# pico_stdlib) is still supplied and linked by the platform's own executable target.
target_link_libraries(AudioPlayer PUBLIC
        FileSystem
)
