cmake_minimum_required(VERSION 3.22.1)

project("xdl")

add_library(xdl SHARED
        xdl_ext.c
        xdl_ext_lzma.cc
        xdl_ext_util.cc
        ../external/xDL/xdl/src/main/cpp/xdl_iterate.c
        ../external/xDL/xdl/src/main/cpp/xdl_linker.c
        ../external/xz-embedded/linux/lib/xz/xz_crc32.c
        ../external/xz-embedded/linux/lib/xz/xz_dec_lzma2.c
        ../external/xz-embedded/linux/lib/xz/xz_dec_stream.c
)
target_include_directories(xdl PRIVATE
        ../external/xDL/xdl/src/main/cpp
        ../external/xz-embedded/linux/include/linux
        ../external/xz-embedded/userspace
)
target_include_directories(xdl PUBLIC
        include
        ../external/xDL/xdl/src/main/cpp/include
)
