add_library(posix_block_device STATIC
        src/PosixBlockDevice.cpp
)

# Paths are resolved relative to this file (not CMAKE_SOURCE_DIR) so this library builds
# identically regardless of which top-level CMakeLists.txt (native or platform/<board>) is
# configuring the project.
target_include_directories(posix_block_device PUBLIC
        include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../app/FileSystem/include
)
