# Qt6 Widgets combined IInputHandler + IDisplay implementation for native/Linux builds.
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_library(QtGui STATIC
        src/QtGui.cpp
)

# AUTOMOC lets CMake invoke Qt's moc on QObject-derived headers/sources so signal/slot
# connections used inside QtGui.cpp work without a manually-authored moc_*.cpp.
set_target_properties(QtGui PROPERTIES
        AUTOMOC ON
)

# Path is resolved relative to this file (not CMAKE_SOURCE_DIR) so this library builds
# identically regardless of which top-level CMakeLists.txt (native or platform/<board>) is
# configuring the project.
target_include_directories(QtGui PUBLIC
        include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(QtGui PUBLIC
        Qt6::Widgets
)
