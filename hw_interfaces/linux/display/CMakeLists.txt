add_library(Display STATIC
        src/Display.cpp
)

# Path is resolved relative to this file (not CMAKE_SOURCE_DIR) so this library builds
# identically regardless of which top-level CMakeLists.txt (native or platform/<board>) is
# configuring the project.
target_include_directories(Display PUBLIC
        include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
