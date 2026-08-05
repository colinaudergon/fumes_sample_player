# Vendored, unmodified upstream FatFs R0.15 core (ChaN). No hardware or Pico SDK dependency here;
# diskio.c is intentionally excluded — app/FileSystem provides its own diskio.cpp glue that binds
# FatFs to the IBlockDevice hardware abstraction instead.
add_library(FatFsCore STATIC
        ff15/source/ff.c
        ff15/source/ffsystem.c
        ff15/source/ffunicode.c
)

target_include_directories(FatFsCore PUBLIC
        ff15/source
)
