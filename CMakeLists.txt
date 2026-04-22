cmake_minimum_required(VERSION 3.16)

find_package(Python3 REQUIRED)
set(ULTRA_SCRIPTS_DIR "${CMAKE_SOURCE_DIR}/scripts")

include(kconfig.cmake)

if (NOT CONFIG_FILE)
    set(CONFIG_FILE "${CMAKE_CURRENT_BINARY_DIR}/.config")

    if (NOT EXISTS "${CONFIG_FILE}" OR IS_DIRECTORY "${CONFIG_FILE}")
        message(
            "Please configure the kernel before attempting to build!\n\n"
            "In order to generate a default configuration file simply run"
            " \"olddefconfig\"\n\n"
            "The following options are available to provide a configuration"
            " file to CMake:\n"
            "- Pass -DCONFIG_FILE=<path> when running cmake\n"
            "- Copy the configuration file into ${CMAKE_CURRENT_BINARY_DIR}\n"
            "- Let ${CMAKE_CURRENT_SOURCE_DIR}/build.py do this for you"
            " automatically"
        )
        message(
            FATAL_ERROR
            "Kernel configuration file missing (see details above)"
        )
    endif ()
else ()
    # Always canonicalize whatever the user gave us
    get_filename_component(CONFIG_FILE "${CONFIG_FILE}" ABSOLUTE)

    if (NOT EXISTS "${CONFIG_FILE}" OR IS_DIRECTORY "${CONFIG_FILE}")
       message(
            FATAL_ERROR
            "Invalid configuration file path \"${CONFIG_FILE}\"!"
       )
    endif ()
endif ()

kconfig_sanitize(${CONFIG_FILE})
kconfig_load(${CONFIG_FILE})

set(ULTRA_ARCH ${CONFIG_ARCH_STRING})
set(ULTRA_TOOLCHAIN ${CONFIG_TOOLCHAIN_STRING})

if (NOT DEFINED ULTRA_TOOLCHAIN_INITIAL)
    set(
        ULTRA_TOOLCHAIN_INITIAL "${ULTRA_TOOLCHAIN}"
        CACHE INTERNAL "The initial toolchain used for the kernel"
    )
elseif (NOT ULTRA_TOOLCHAIN_INITIAL STREQUAL "${ULTRA_TOOLCHAIN}")
    message(
        FATAL_ERROR
        "Toolchain changed after the initial CMake configuration pass!\n"
        "CMake won't pick up this change, please rerun it with --fresh or "
        "recreate the build directory from scratch\n"
        "(Was: ${ULTRA_TOOLCHAIN_INITIAL}, now: ${ULTRA_TOOLCHAIN})"
    )
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

add_subdirectory(kernel)
