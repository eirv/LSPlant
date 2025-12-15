cmake_minimum_required(VERSION 3.18.1)
project("lsplant_test")

set(CMAKE_CXX_STANDARD 23)

set(RV64HOOK_BUILD_SHARED ON CACHE INTERNAL "" FORCE)
set(RV64HOOK_BUILD_STATIC OFF CACHE INTERNAL "" FORCE)
set(DOBBY_DEBUG OFF CACHE INTERNAL "" FORCE)
set(Plugin.SymbolResolver OFF CACHE INTERNAL "" FORCE)

add_subdirectory(../../../../lsplant/src/main/jni lsplant)
add_subdirectory(xDL)
add_subdirectory(external/riscv64-inline-hook)

add_library(test SHARED test.cc)
set_target_properties(test PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_link_libraries(test PRIVATE log lsplant xdl rv64hook)

if (NOT ANDROID_ABI STREQUAL "riscv64")
    add_subdirectory(external/Dobby)
    target_compile_definitions(dobby PRIVATE DOBBY_LOGGING_DISABLE)
    target_link_libraries(test PRIVATE dobby)
endif ()
