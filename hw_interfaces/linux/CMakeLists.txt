# Each Linux hw_interfaces implementation lives in its own folder (mirroring
# hw_interfaces/rp2040/*), with its own CMakeLists.txt/include/src.
add_subdirectory(audio_codec)
add_subdirectory(posix_block_device)
add_subdirectory(display)
add_subdirectory(user_input)

