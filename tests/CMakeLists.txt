cmake_minimum_required(VERSION 3.6)

project(Tests)

set(OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY         "${OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIRECTORY}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG   "${OUTPUT_DIRECTORY}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIRECTORY}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG   "${OUTPUT_DIRECTORY}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIRECTORY}")

set(KERNEL_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../kernel")
set(COPIED_KERNEL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/kernel-source")
set(COPIED_KERNEL_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/kernel-include")
set_property(GLOBAL PROPERTY EXTERNAL_KERNEL_FILES)

file(MAKE_DIRECTORY ${COPIED_KERNEL_SOURCES})
file(MAKE_DIRECTORY ${COPIED_KERNEL_INCLUDES})

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${COPIED_KERNEL_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/synthetic-include
)

function(KERNEL_FILE)
    cmake_parse_arguments(
        ARG
        ""
        "SOURCE_PATH;SOURCE_FILE;INCLUDE_PATH;INCLUDE_FILE"
        ""
        ${ARGN}
    )

    get_property(
        EXTERNAL_KERNEL_FILES_LOCAL
        GLOBAL PROPERTY EXTERNAL_KERNEL_FILES
    )

    if (ARG_SOURCE_FILE)
        set(
            OUT_FILE_PATH
            "${COPIED_KERNEL_SOURCES}/${ARG_SOURCE_PATH}/${ARG_SOURCE_FILE}"
        )

        set(
            IN_FILE_PATH
            "${KERNEL_ROOT_DIR}/${ARG_SOURCE_PATH}/${ARG_SOURCE_FILE}"
        )

        list(APPEND EXTERNAL_KERNEL_FILES_LOCAL ${OUT_FILE_PATH})
        add_custom_command(
            OUTPUT ${OUT_FILE_PATH}
            COMMAND
                ${CMAKE_COMMAND} -E copy "${IN_FILE_PATH}" "${OUT_FILE_PATH}"
            MAIN_DEPENDENCY "${IN_FILE_PATH}"
        )
    endif()

    if (ARG_INCLUDE_FILE)
        set(
            OUT_FILE_PATH
            "${COPIED_KERNEL_INCLUDES}/${ARG_INCLUDE_PATH}/${ARG_INCLUDE_FILE}"
        )

        set(
            IN_FILE_PATH
            "${KERNEL_ROOT_DIR}/include/${ARG_INCLUDE_PATH}/${ARG_INCLUDE_FILE}"
        )

        list(APPEND EXTERNAL_KERNEL_FILES_LOCAL ${OUT_FILE_PATH})
        add_custom_command(
            OUTPUT ${OUT_FILE_PATH}
            COMMAND
                ${CMAKE_COMMAND} -E copy "${IN_FILE_PATH}" "${OUT_FILE_PATH}"
            MAIN_DEPENDENCY "${IN_FILE_PATH}"
        )
    endif()

    set_property(
        GLOBAL PROPERTY EXTERNAL_KERNEL_FILES ${EXTERNAL_KERNEL_FILES_LOCAL}
    )
endfunction(KERNEL_FILE)

KERNEL_FILE(
    SOURCE_PATH "memory" SOURCE_FILE "boot_alloc.c"
    INCLUDE_PATH "boot" INCLUDE_FILE "alloc.h"
)
KERNEL_FILE(SOURCE_FILE "param.c"  INCLUDE_FILE "param.h")
KERNEL_FILE(INCLUDE_PATH "common" INCLUDE_FILE "helpers.h")
KERNEL_FILE(INCLUDE_PATH "common" INCLUDE_FILE "types.h")
KERNEL_FILE(INCLUDE_PATH "common" INCLUDE_FILE "string.h")
KERNEL_FILE(INCLUDE_PATH "common" INCLUDE_FILE "attributes.h")
KERNEL_FILE(INCLUDE_PATH "common" INCLUDE_FILE "minmax.h")
KERNEL_FILE(INCLUDE_PATH "common" INCLUDE_FILE "align.h")
KERNEL_FILE(INCLUDE_PATH "common" INCLUDE_FILE "error.h")
KERNEL_FILE(INCLUDE_PATH "common" INCLUDE_FILE "ctype.h")
KERNEL_FILE(
    SOURCE_PATH "common" SOURCE_FILE "string_container.c"
    INCLUDE_PATH "common" INCLUDE_FILE "string_container.h"
)
KERNEL_FILE(
    SOURCE_PATH "common" SOURCE_FILE "conversions.c"
    INCLUDE_PATH "common" INCLUDE_FILE "conversions.h"
)
KERNEL_FILE(
    SOURCE_PATH "common" SOURCE FILE "format.c"
    INCLUDE_PATH "common" INCLUDE_FILE "format.h"
)
KERNEL_FILE(INCLUDE_PATH "boot" INCLUDE_FILE "boot.h")
KERNEL_FILE(INCLUDE_PATH "boot" INCLUDE_FILE "ultra_protocol.h")
KERNEL_FILE(INCLUDE_FILE "log.h")
KERNEL_FILE(INCLUDE_FILE "bug.h")
KERNEL_FILE(INCLUDE_FILE "panic.h")
KERNEL_FILE(INCLUDE_FILE "linker.h")

get_property(EXTERNAL_KERNEL_FILES_LOCAL GLOBAL PROPERTY EXTERNAL_KERNEL_FILES)
add_custom_target(external_files DEPENDS ${EXTERNAL_KERNEL_FILES_LOCAL})

set(TEST_SOURCES "")

macro(add_test_cases)
    foreach (SOURCE_FILE ${ARGV})
        list(APPEND TEST_SOURCES "test-cases/${SOURCE_FILE}")
    endforeach()
endmacro()

include("test-cases/files.cmake")

add_executable(run_tests run_tests.cpp kernel_helpers.cpp test_harness.cpp ${TEST_SOURCES})
set_property(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PROPERTY VS_STARTUP_PROJECT
    run_tests
)
add_dependencies(run_tests external_files)
target_compile_definitions(run_tests PUBLIC ULTRA_TEST)

if (MSVC)
    target_compile_options(
        run_tests
        PRIVATE
        # nonstandard extension used: zero-sized array in struct/union
        /wd4200
        # function assumed not to throw an exception but does
        /wd4297
        # unary minus operator applied to unsigned type, result still unsigned
        /wd4146
    )
endif ()

set_property(TARGET run_tests PROPERTY C_STANDARD 17)
set_property(TARGET run_tests PROPERTY CXX_STANDARD 20)
