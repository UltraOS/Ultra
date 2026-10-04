cmake_minimum_required(VERSION 3.16)

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
    ${COPIED_KERNEL_SOURCES}/arch/x86_64/include
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
KERNEL_FILE(
    SOURCE_FILE "log_ring.c" INCLUDE_FILE "log_ring.h"
)
KERNEL_FILE(
    SOURCE_FILE "drivers/smbios.c" INCLUDE_FILE "smbios.h"
)
KERNEL_FILE(INCLUDE_FILE "detail/log_ring.h")
KERNEL_FILE(INCLUDE_FILE "smbios_structures.h")
KERNEL_FILE(SOURCE_FILE "param.c"  INCLUDE_FILE "param.h")
KERNEL_FILE(INCLUDE_PATH "common" INCLUDE_FILE "helpers.h")
KERNEL_FILE(INCLUDE_PATH "common" INCLUDE_FILE "types.h")
KERNEL_FILE(INCLUDE_PATH "common" INCLUDE_FILE "attributes.h")
KERNEL_FILE(INCLUDE_PATH "common" INCLUDE_FILE "minmax.h")
KERNEL_FILE(INCLUDE_PATH "common" INCLUDE_FILE "align.h")
KERNEL_FILE(INCLUDE_PATH "common" INCLUDE_FILE "atomic.h")
KERNEL_FILE(INCLUDE_PATH "common" INCLUDE_FILE "ctype.h")
KERNEL_FILE(INCLUDE_PATH "common" INCLUDE_FILE "byte_order.h")
KERNEL_FILE(INCLUDE_PATH "common" INCLUDE_FILE "list.h")
KERNEL_FILE(INCLUDE_PATH "common" INCLUDE_FILE "slist.h")
KERNEL_FILE(
    SOURCE_PATH "common" SOURCE_FILE "string_container.c"
    INCLUDE_PATH "common" INCLUDE_FILE "string_container.h"
)
KERNEL_FILE(
    SOURCE_PATH "common" SOURCE_FILE "conversions.c"
    INCLUDE_PATH "common" INCLUDE_FILE "conversions.h"
)
KERNEL_FILE(
    SOURCE_PATH "common" SOURCE_FILE "bit.c"
    INCLUDE_PATH "common" INCLUDE_FILE "bit.h"
)
KERNEL_FILE(
    SOURCE_FILE "cpu_mask.c"
    INCLUDE_FILE "cpu_mask.h"
)
KERNEL_FILE(SOURCE_PATH "arch/x86_64" SOURCE_FILE "vector_alloc.c")
KERNEL_FILE(SOURCE_PATH "irq" SOURCE_FILE "chip.c")
KERNEL_FILE(SOURCE_FILE "console.c" INCLUDE_FILE "console.h")
KERNEL_FILE(INCLUDE_PATH "pci" INCLUDE_FILE "address.h")
KERNEL_FILE(SOURCE_PATH "irq" SOURCE_FILE "domain.c")
KERNEL_FILE(SOURCE_PATH "irq" SOURCE_FILE "request.c")
KERNEL_FILE(SOURCE_PATH "irq" SOURCE_FILE "flow.c")
KERNEL_FILE(INCLUDE_FILE "irq.h")
KERNEL_FILE(INCLUDE_PATH "private" INCLUDE_FILE "irq.h")
KERNEL_FILE(
    SOURCE_PATH "arch/x86_64/include/arch/private" SOURCE_FILE "idt.h"
)
KERNEL_FILE(
    SOURCE_PATH "arch/x86_64/include/arch/private" SOURCE_FILE "vectors.h"
)
KERNEL_FILE(
    SOURCE_PATH "arch/x86_64/include/arch/private"
    SOURCE_FILE "vector_alloc.h"
)
KERNEL_FILE(
    SOURCE_PATH "common" SOURCE_FILE "rb_tree.c"
    INCLUDE_PATH "common" INCLUDE_FILE "rb_tree.h"
)
KERNEL_FILE(INCLUDE_PATH "detail/common" INCLUDE_FILE "rb_tree.h")
KERNEL_FILE(INCLUDE_PATH "common" INCLUDE_FILE "rb_tree_aggregated.h")
KERNEL_FILE(
    SOURCE_PATH "memory" SOURCE_FILE "alloc.c"
    INCLUDE_PATH "memory" INCLUDE_FILE "alloc.h"
)
KERNEL_FILE(INCLUDE_PATH "memory" INCLUDE_FILE "alloc_cache.h")
KERNEL_FILE(
    SOURCE_PATH "memory" SOURCE_FILE "buddy.c"
    INCLUDE_PATH "memory" INCLUDE_FILE "buddy.h"
)
KERNEL_FILE(
    SOURCE_PATH "memory" SOURCE_FILE "valloc.c"
    INCLUDE_PATH "memory" INCLUDE_FILE "valloc.h"
)
KERNEL_FILE(
    SOURCE_PATH "time" SOURCE_FILE "counter_device.c"
    INCLUDE_PATH "time" INCLUDE_FILE "counter_device.h"
)
KERNEL_FILE(INCLUDE_PATH "private/time" INCLUDE_FILE "counter_device.h")
KERNEL_FILE(INCLUDE_PATH "time" INCLUDE_FILE "time.h")
KERNEL_FILE(
    SOURCE_PATH "time" SOURCE_FILE "keeper.c"
    INCLUDE_PATH "private/time" INCLUDE_FILE "keeper.h"
)
KERNEL_FILE(INCLUDE_PATH "time" INCLUDE_FILE "units.h")
KERNEL_FILE(INCLUDE_PATH "memory" INCLUDE_FILE "page.h")
KERNEL_FILE(INCLUDE_PATH "memory" INCLUDE_FILE "bug.h")
KERNEL_FILE(INCLUDE_PATH "memory" INCLUDE_FILE "alloc_behavior.h")
KERNEL_FILE(
    SOURCE_PATH "memory" SOURCE_FILE "units.c"
    INCLUDE_PATH "memory" INCLUDE_FILE "units.h"
)
KERNEL_FILE(INCLUDE_PATH "private" INCLUDE_FILE "buddy.h")
KERNEL_FILE(INCLUDE_PATH "boot" INCLUDE_FILE "boot.h")
KERNEL_FILE(INCLUDE_PATH "boot" INCLUDE_FILE "ultra_protocol.h")
KERNEL_FILE(INCLUDE_FILE "config_helpers.h")
KERNEL_FILE(INCLUDE_FILE "log.h")
KERNEL_FILE(INCLUDE_FILE "bug.h")
KERNEL_FILE(INCLUDE_PATH "detail" INCLUDE_FILE "bug.h")
KERNEL_FILE(INCLUDE_FILE "refcount.h" SOURCE_FILE "refcount.c")
KERNEL_FILE(INCLUDE_PATH "detail" INCLUDE_FILE "refcount.h")
KERNEL_FILE(INCLUDE_FILE "panic.h")
KERNEL_FILE(INCLUDE_FILE "linker.h")
KERNEL_FILE(INCLUDE_FILE "linker_common.ld.h")
KERNEL_FILE(INCLUDE_PATH "locking" INCLUDE_FILE "seq.h")
KERNEL_FILE(INCLUDE_PATH "detail" INCLUDE_FILE "init_level.h")
KERNEL_FILE(INCLUDE_FILE "preempt.h")
KERNEL_FILE(INCLUDE_PATH "detail" INCLUDE_FILE "preempt.h")
KERNEL_FILE(INCLUDE_PATH "private/arch" INCLUDE_FILE "preempt.h")
KERNEL_FILE(INCLUDE_PATH "detail/sched" INCLUDE_FILE "rr.h")
KERNEL_FILE(
    INCLUDE_PATH "sched" INCLUDE_FILE "task.h"
    SOURCE_PATH "sched" SOURCE_FILE "task.c"
)
KERNEL_FILE(INCLUDE_PATH "detail/sched" INCLUDE_FILE "task.h")
KERNEL_FILE(INCLUDE_PATH "private/sched" INCLUDE_FILE "task.h")
KERNEL_FILE(
    INCLUDE_PATH "private/sched" INCLUDE_FILE "core.h"
    SOURCE_PATH "sched" SOURCE_FILE "core.c"
)
KERNEL_FILE(INCLUDE_PATH "private/sched" INCLUDE_FILE "classes.h")
KERNEL_FILE(
    INCLUDE_PATH "private/sched" INCLUDE_FILE "rr.h"
    SOURCE_PATH "sched" SOURCE_FILE "rr.c"
)
KERNEL_FILE(
    INCLUDE_PATH "private/sched" INCLUDE_FILE "idle.h"
    SOURCE_PATH "sched" SOURCE_FILE "idle.c"
)
KERNEL_FILE(
    INCLUDE_PATH "locking" INCLUDE_FILE "spinlock.h"
    SOURCE_PATH "locking" SOURCE_FILE "spinlock.c"
)
KERNEL_FILE(INCLUDE_PATH "detail/locking" INCLUDE_FILE "spinlock.h")
KERNEL_FILE(
    INCLUDE_PATH "locking" INCLUDE_FILE "mutex.h"
    SOURCE_PATH "locking" SOURCE_FILE "mutex.c"
)
KERNEL_FILE(INCLUDE_PATH "detail/locking" INCLUDE_FILE "mutex.h")
KERNEL_FILE(INCLUDE_PATH "memory" INCLUDE_FILE "vm_flags.h")
KERNEL_FILE(INCLUDE_PATH "memory" INCLUDE_FILE "page_table.h")
KERNEL_FILE(INCLUDE_PATH "memory" INCLUDE_FILE "address_space.h")
KERNEL_FILE(INCLUDE_PATH "memory" INCLUDE_FILE "tlb.h")

get_property(EXTERNAL_KERNEL_FILES_LOCAL GLOBAL PROPERTY EXTERNAL_KERNEL_FILES)
add_custom_target(external_files DEPENDS ${EXTERNAL_KERNEL_FILES_LOCAL})

set(TEST_SOURCES "")

macro(add_test_cases)
    foreach (SOURCE_FILE ${ARGV})
        list(APPEND TEST_SOURCES "test-cases/${SOURCE_FILE}")
    endforeach()
endmacro()

include("test-cases/files.cmake")

add_executable(
    run_tests
    run_tests.cpp kernel_helpers.cpp test_harness.cpp memory_map.c ${TEST_SOURCES}
    task_state.c sched_alloc.c
    kernel-source/locking/spinlock.c kernel-source/memory/units.c
    kernel-source/common/bit.c kernel-source/locking/mutex.c
    kernel-source/common/string_container.c kernel-source/common/conversions.c
    kernel-source/refcount.c
)
set_property(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PROPERTY VS_STARTUP_PROJECT
    run_tests
)
add_dependencies(run_tests external_files)
target_compile_definitions(
    run_tests
    PUBLIC
    ULTRA_TEST
    ULTRA_ARCH="Usermode"
    ULTRA_ARCH_LITTLE_ENDIAN
    ULTRA_ARCH_PHYS_ADDR_WIDTH=8
    ULTRA_ARCH_WIDTH=8
)

set(TEST_LD_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/link.ld.template")
set(TEST_LD_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/link.ld")

add_custom_command(
    OUTPUT ${TEST_LD_SCRIPT}
    COMMAND
        ${CMAKE_C_COMPILER} -E -xc -P -D__ASSEMBLER__
        "-I$<JOIN:$<TARGET_PROPERTY:run_tests,INCLUDE_DIRECTORIES>,;-I>"
        "-D$<JOIN:$<TARGET_PROPERTY:run_tests,COMPILE_DEFINITIONS>,;-D>"
        ${TEST_LD_TEMPLATE} -o ${TEST_LD_SCRIPT}
    DEPENDS
        ${TEST_LD_TEMPLATE}
        ${EXTERNAL_KERNEL_FILES_LOCAL}
    COMMAND_EXPAND_LISTS
)

add_custom_target(link_script DEPENDS ${TEST_LD_SCRIPT})
add_dependencies(link_script external_files)
add_dependencies(run_tests link_script)

target_link_options(run_tests PRIVATE "-Wl,-T,${TEST_LD_SCRIPT}")
set_property(TARGET run_tests APPEND PROPERTY LINK_DEPENDS ${TEST_LD_SCRIPT})

set_property(TARGET run_tests PROPERTY C_STANDARD 23)
set_property(TARGET run_tests PROPERTY CXX_STANDARD 20)
