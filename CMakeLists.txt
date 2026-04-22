cmake_minimum_required(VERSION 3.16)

find_package(Python3 REQUIRED)

if (NOT ULTRA_ARCH)
    set(ULTRA_ARCH x86_64)
elseif (NOT ULTRA_ARCH STREQUAL "x86_64" AND
        NOT ULTRA_ARCH STREQUAL "aarch64")
    message(FATAL_ERROR "Invalid architecture ${ULTRA_ARCH}")
endif ()

if (NOT ULTRA_TOOLCHAIN)
    set(ULTRA_TOOLCHAIN clang)
endif ()

set(ULTRA_TOOLCHAIN_DIR "${CMAKE_SOURCE_DIR}/toolchain")
include("${ULTRA_TOOLCHAIN_DIR}/toolchain_${ULTRA_TOOLCHAIN}.cmake")

set(ULTRA_SCRIPTS_DIR "${CMAKE_SOURCE_DIR}/scripts")

project(Ultra ${ULTRA_LANGUAGES})

execute_process(
    COMMAND
    git describe --always --dirty
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE ULTRA_GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if (NOT ULTRA_GIT_SHA)
    set(ULTRA_GIT_SHA "0000000")
endif ()

option(ULTRA_CONFIG_LTO, "Enables link time optimizations")

add_subdirectory(kernel)
