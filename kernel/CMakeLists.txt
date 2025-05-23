set(ULTRA_KERNEL "kernel-${ULTRA_ARCH_EXECUTION_MODE}")

add_executable(${ULTRA_KERNEL})

set_target_properties(
    ${ULTRA_KERNEL}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}"
)
target_sources(
    ${ULTRA_KERNEL}
    PRIVATE
    alloc.c
    entry.c
    panic.c
    console.c
    log.c
    io.c
)
target_include_directories(
    ${ULTRA_KERNEL}
    PRIVATE
    include
)
ultra_target_compile_options(
    ${ULTRA_KERNEL}
    -std=gnu17
    -ffreestanding
    -ggdb3
    -Wall
    -Wextra
    -Werror
)
ultra_target_link_options(
    ${ULTRA_KERNEL}
    -nostdlib
    --orphan-handling=error
)

string(TOUPPER ${ULTRA_ARCH} ULTRA_ARCH_POSTFIX)
string(TOUPPER ${ULTRA_ARCH_EXECUTION_MODE} ULTRA_ARCH_EXECUTION_MODE_POSTFIX)
target_compile_definitions(
    ${ULTRA_KERNEL}
    PUBLIC
    ULTRA_ARCH_${ULTRA_ARCH_POSTFIX}
    ULTRA_ARCH_EXECUTION_MODE_${ULTRA_ARCH_EXECUTION_MODE_POSTFIX}

    ULTRA_GIT_SHA="${ULTRA_GIT_SHA}"
    ULTRA_ARCH_STRING="${ULTRA_ARCH}"
    ULTRA_ARCH_EXECUTION_MODE_STRING="${ULTRA_ARCH_EXECUTION_MODE}"
)

# We have to do this since ${CMAKE_CURRENT_FUNCTION_LIST_DIR}
# was introduced in CMake 3.17, which is relatively recent.
# (e.g. Ubuntu 20.04 ships with 3.16.3 by default)
set(ULTRA_MODULE_LD_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/link_module.ld")

include(module.cmake)
include(ld_template.cmake)

add_subdirectory(common)
add_subdirectory(arch/${ULTRA_ARCH})
