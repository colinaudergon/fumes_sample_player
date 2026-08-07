add_library(FileManager STATIC
src/FileManager.cpp
)

# Paths are resolved relative to this file (not CMAKE_SOURCE_DIR) so this library builds
# identically whether it's pulled in by the root (native) CMakeLists.txt or by a
# platform/<board> one, e.g. platform/rp2040/CMakeLists.txt.
target_include_directories(FileManager PUBLIC
        include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../hw_interfaces/include
)

target_link_libraries(FileManager PUBLIC
        FileSystem
)
