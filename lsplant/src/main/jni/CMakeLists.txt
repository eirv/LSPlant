cmake_minimum_required(VERSION 3.28)
project(lsplant LANGUAGES CXX)

option(LSPLANT_BUILD_SHARED "Build lsplant as shared library" ON)
option(LSPLANT_BUILD_STATIC "Build lsplant as static library" OFF)
option(LSPLANT_USE_MODULES "Use C++ modules" OFF)
option(LSPLANT_STANDALONE "Build as standalone" OFF)

set(CMAKE_CXX_STANDARD 23)

find_program(CCACHE ccache)
if (CCACHE)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE})
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE})
endif ()

if (NOT DEBUG_SYMBOLS_PATH)
    set(DEBUG_SYMBOLS_PATH ${CMAKE_BINARY_DIR}/symbols CACHE PATH "Debug symbols output directory")
endif ()

function(setup_module_target target_name)
    if (LSPLANT_USE_MODULES)
        set_target_properties(${target_name} PROPERTIES CXX_SCAN_FOR_MODULES ON)
        target_sources(${target_name}
                PUBLIC FILE_SET ms TYPE CXX_MODULES FILES ${MODULE_SOURCES}
                PUBLIC FILE_SET CXX_MODULES FILES ${MODULE_INTERFACES}
        )
        target_compile_definitions(${target_name} PRIVATE LSPLANT_USE_MODULES)
        if (TARGET ${target_name} AND "${target_name}" STREQUAL "${PROJECT_NAME}")
            target_link_options(${target_name} PRIVATE
                    -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/lsplant.map.txt
            )
        endif ()
    endif ()
endfunction()

function(setup_compiler_options target_name)
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory ${DEBUG_SYMBOLS_PATH}/${ANDROID_ABI}
                COMMAND ${CMAKE_OBJCOPY} --only-keep-debug
                $<TARGET_FILE:${target_name}>
                ${DEBUG_SYMBOLS_PATH}/${ANDROID_ABI}/${PROJECT_NAME}
                VERBATIM
        )
    else ()
        target_compile_options(${target_name} PRIVATE
                $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-flto>
                $<$<CXX_COMPILER_ID:MSVC>:/GL>
        )
        target_link_options(${target_name} PRIVATE
                -Wl,--as-needed -Wl,--icf=all
        )
        add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory ${DEBUG_SYMBOLS_PATH}/${ANDROID_ABI}
                COMMAND ${CMAKE_OBJCOPY} --only-keep-debug
                $<TARGET_FILE:${target_name}>
                ${DEBUG_SYMBOLS_PATH}/${ANDROID_ABI}/${PROJECT_NAME}
                COMMAND ${CMAKE_STRIP} --strip-all $<TARGET_FILE:${target_name}>
                VERBATIM
        )
    endif ()
endfunction()

function(setup_common_target target_name)
    target_include_directories(${target_name}
            PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/demodularize
    )
    target_link_libraries(${target_name}
            PUBLIC dex_builder_static log
    )
    setup_module_target(${target_name})
endfunction()

if (LSPLANT_STANDALONE)
    find_package(cxx REQUIRED CONFIG)
    link_libraries(cxx::cxx)
endif ()

set(DEX_BUILDER_BUILD_SHARED OFF CACHE INTERNAL "")
add_subdirectory(external/dex_builder)

if (LSPLANT_USE_MODULES)
    set(SOURCES lsplant.cc)
    file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS "*.cxx")
    file(GLOB_RECURSE MODULE_INTERFACES CONFIGURE_DEPENDS "*.ixx")
    set(EXCLUDE_PATTERN "${PROJECT_SOURCE_DIR}/.external.*")
    list(FILTER MODULE_SOURCES EXCLUDE REGEX "${EXCLUDE_PATTERN}")
    list(FILTER MODULE_INTERFACES EXCLUDE REGEX "${EXCLUDE_PATTERN}")
else ()
    set(SOURCES demodularize/lsplant.cc)
endif ()

if (LSPLANT_BUILD_SHARED)
    message(STATUS "Building lsplant as a shared library")
    add_library(${PROJECT_NAME} SHARED ${SOURCES})
    setup_common_target(${PROJECT_NAME})
    setup_compiler_options(${PROJECT_NAME})
endif ()

if (LSPLANT_BUILD_STATIC)
    add_library(${PROJECT_NAME}_static STATIC ${SOURCES})
    setup_common_target(${PROJECT_NAME}_static)
    set_target_properties(${PROJECT_NAME}_static PROPERTIES
            OUTPUT_NAME ${PROJECT_NAME}
    )
    add_library(${PROJECT_NAME}::static ALIAS ${PROJECT_NAME}_static)
endif ()
