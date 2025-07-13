cmake_minimum_required(VERSION 3.16)

if (NOT ULTRA_ARCH)
    set(ULTRA_ARCH x86)
elseif (NOT ULTRA_ARCH STREQUAL "x86" AND
        NOT ULTRA_ARCH STREQUAL "arm")
    message(FATAL_ERROR "Invalid architecture ${ULTRA_ARCH}")
endif ()

if (NOT ULTRA_ARCH_EXECUTION_MODE)
    if (ULTRA_ARCH STREQUAL "x86")
        set(ULTRA_ARCH_EXECUTION_MODE "x86_64")
    elseif (ULTRA_ARCH STREQUAL "arm")
        set(ULTRA_ARCH_EXECUTION_MODE "aarch64")
    endif ()
endif ()

set(ULTRA_FULL_ARCH "${ULTRA_ARCH}/${ULTRA_ARCH_EXECUTION_MODE}")

if (ULTRA_ARCH STREQUAL "x86")
    set(ULTRA_VALID_EXECUTION_MODES i686 x86_64)
elseif (ULTRA_ARCH STREQUAL "arm")
    set(ULTRA_VALID_EXECUTION_MODES aarch32 aarch64)
endif ()

if (NOT ULTRA_ARCH_EXECUTION_MODE IN_LIST ULTRA_VALID_EXECUTION_MODES)
    message(
        FATAL_ERROR
        "Invalid arch/execution-mode ${ULTRA_FULL_ARCH}"
    )
endif ()

if ($ENV{CLION_IDE})
    set(ULTRA_TOOLCHAIN dummy)
    message("Detected CLion, using a dummy toolchain file")
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
