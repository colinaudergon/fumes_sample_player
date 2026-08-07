add_library(AudioEffects STATIC
        src/EffectController.cpp
        effects/distorsion.cpp
)

# Paths are resolved relative to this file (not CMAKE_SOURCE_DIR) so this library builds
# identically whether it's pulled in by the root (native) CMakeLists.txt or by a
# platform/<board> one, e.g. platform/rp2040/CMakeLists.txt.
target_include_directories(AudioEffects PUBLIC
        include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../hw_interfaces/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../AudioPlayer/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../AudioPlayer/audio/wav
)