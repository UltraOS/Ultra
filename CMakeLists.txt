cmake_minimum_required(VERSION 3.16)

if (NOT ULTRA_ARCH)
	set(ULTRA_ARCH x86_64)
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

add_subdirectory(kernel)
