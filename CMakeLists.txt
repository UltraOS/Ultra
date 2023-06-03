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

add_subdirectory(kernel)
