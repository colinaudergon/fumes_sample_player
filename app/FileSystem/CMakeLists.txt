add_library(FileSystem STATIC
        src/diskio.cpp
        src/FatFsFileSystemAdapter.cpp
        src/FileSystemFactory.cpp
        src/IBlockDevice.cpp
)

# Paths are resolved relative to this file (not CMAKE_SOURCE_DIR) so this library builds
# identically whether it's pulled in by the root (native) CMakeLists.txt or by a
# platform/<board> one, e.g. platform/rp2040/CMakeLists.txt.
target_include_directories(FileSystem PUBLIC
        include
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# FileSystem only depends on FatFsCore and the hardware-agnostic IBlockDevice/IFileSystem
# interfaces; the concrete IBlockDevice implementation (and any platform-specific dependency
# such as pico_stdlib) is supplied and linked by the platform's own executable target.
target_link_libraries(FileSystem PUBLIC
        FatFsCore
)
