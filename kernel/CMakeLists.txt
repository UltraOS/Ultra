include(symbols.cmake)
include(target_helpers.cmake)
include(module.cmake)
include(ld_template.cmake)

# The base kernel executable that contains an empty stub symbol table
set(
    ULTRA_KERNEL_BASE
    "kernel-${ULTRA_ARCH_EXECUTION_MODE}-base"
)
add_executable(${ULTRA_KERNEL_BASE})

# Kernel objects shared among all ULTRA_KERNEL_TARGETS
set(ULTRA_KERNEL_OBJECTS "kernel-objects")
add_library(${ULTRA_KERNEL_OBJECTS} OBJECT)

# Kernel "stages" used to embed the symbol table in a stable way
set(
    ULTRA_KERNEL_TARGETS
    ${ULTRA_KERNEL_BASE}
)

# Number of times to re-link the kernel to stabilize symbols
set(ULTRA_NUM_RELINKS 1)
if (ULTRA_CONFIG_LTO)
    set(ULTRA_NUM_RELINKS 2)
endif ()

setup_symbol_table_link_steps(${ULTRA_NUM_RELINKS})

ultra_link_libraries(${ULTRA_KERNEL_OBJECTS})

ultra_properties(
    RUNTIME_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}"
)
ultra_sources(
    entry.c
    panic.c
    console.c
    log.c
    io.c
    symbols.c
)
ultra_include_directories(include)

ultra_compile_options(
    -std=gnu17
    -ffreestanding
    -ggdb3
    -Wall
    -Wextra
    -Werror
)
if (ULTRA_CONFIG_LTO)
    ultra_compile_options(-flto=full)
endif ()

ultra_link_options(
    -nostdlib
    --orphan-handling=error
)

string(TOUPPER ${ULTRA_ARCH} ULTRA_ARCH_POSTFIX)
string(TOUPPER ${ULTRA_ARCH_EXECUTION_MODE} ULTRA_ARCH_EXECUTION_MODE_POSTFIX)

ultra_compile_definitions(
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

add_subdirectory(common)
add_subdirectory(memory)
add_subdirectory(arch/${ULTRA_ARCH})
