include(symbols.cmake)
include(target_helpers.cmake)
include(module.cmake)
include(ld_template.cmake)

# The base kernel executable that contains an empty stub symbol table
set(
    ULTRA_KERNEL_BASE
    "kernel-${ULTRA_ARCH}-base"
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

if (CONFIG_ARCH_UNKNOWN_BIT_WIDTH)
        message(
        FATAL_ERROR
        "Please fix your achitecture's Kconfig to specify bitness"
    )
endif ()

if (CONFIG_ARCH_LITTLE_ENDIAN)
    ultra_compile_definitions(ULTRA_ARCH_LITTLE_ENDIAN)
elseif (CONFIG_ARCH_BIG_ENDIAN)
    ultra_compile_definitions(ULTRA_ARCH_BIG_ENDIAN)
else ()
    message(
        FATAL_ERROR
        "Please fix your achitecture's Kconfig to specify endianness"
    )
endif ()

ultra_compile_definitions(
    ULTRA_ARCH_WIDTH=${CONFIG_ARCH_WIDTH}
    ULTRA_ARCH_PHYS_ADDR_WIDTH=${CONFIG_ARCH_PHYS_ADDR_WIDTH}
    ULTRA_ARCH="${CONFIG_ARCH_STRING}"
)

# A list of all kernel object libraries (kernel itself + compiled-in modules)
set_property(GLOBAL PROPERTY ULTRA_OBJECT_TARGETS "${ULTRA_KERNEL_OBJECTS}")

# Number of times to re-link the kernel to stabilize symbols
set(ULTRA_NUM_RELINKS 1)
if (CONFIG_LTO)
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
    symbols.c
    spinlock.c
    unwind.c
    param.c
    log_ring.c
    per_cpu.c
    smp.c
    abortable_instructions.c
    init_level.c
)
ultra_include_directories(
    include
    # For auto-generated headers like config.h
    ${CMAKE_BINARY_DIR}
)

ultra_compile_options(
    -fno-pic
    -fno-pie
    -std=gnu23
    -ffreestanding
    -fasynchronous-unwind-tables
    -ggdb3
    -Wall
    -Wextra
    -Werror
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wimplicit-fallthrough
    -fomit-frame-pointer
)

if (CONFIG_LTO)
    ultra_compile_options(${ULTRA_TOOLCHAIN_LTO_FLAGS})
endif ()

ultra_compile_options(-O${CONFIG_OPTIMIZATION_LEVEL})

ultra_link_options(
    -nostdlib
    -Wl,--no-pie
    -Wl,--eh-frame-hdr
    -Wl,--orphan-handling=error
    -Wl,--build-id=none
    -Wl,-z,noexecstack
)

ultra_compile_definitions(ULTRA_GIT_SHA="${ULTRA_GIT_SHA}")

add_subdirectory(arch/${ULTRA_ARCH})
add_subdirectory(common)
add_subdirectory(memory)
add_subdirectory(drivers)

set(ULTRA_ALL_OBJECTS "")
get_property(ULTRA_OBJECT_TARGETS_FINAL GLOBAL PROPERTY ULTRA_OBJECT_TARGETS)

foreach(obj IN LISTS ULTRA_OBJECT_TARGETS_FINAL)
    list(APPEND ULTRA_ALL_OBJECTS "$<TARGET_OBJECTS:${obj}>")
endforeach()

set(
    ULTRA_COMBINED_OBJ
    "${CMAKE_CURRENT_BINARY_DIR}/combined_${ULTRA_KERNEL_OBJECTS}.o"
)
set(
    FAI_CHECK_SCRIPT
    "${CMAKE_SOURCE_DIR}/scripts/check_free_after_init_references.py"
)

add_custom_command(
    TARGET ${ULTRA_KERNEL_BASE} PRE_LINK

    COMMAND ${CMAKE_LINKER} -r ${ULTRA_ALL_OBJECTS} -o ${ULTRA_COMBINED_OBJ}
    COMMAND Python3::Interpreter ${FAI_CHECK_SCRIPT} ${ULTRA_COMBINED_OBJ}

    COMMENT
        "Verifying there are no illegal references to free-after-init data..."
    VERBATIM
    COMMAND_EXPAND_LISTS
)
