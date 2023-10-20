if (ULTRA_ARCH_EXECUTION_MODE STREQUAL "i686")
    target_compile_definitions(${ULTRA_KERNEL} PUBLIC ULTRA_ARCH_WIDTH=4)
else ()
    target_compile_definitions(${ULTRA_KERNEL} PUBLIC ULTRA_ARCH_WIDTH=8)
endif()

# Hardcode 8 for now, even for i686 (unconditionally enable PAE if it's present)
target_compile_definitions(${ULTRA_KERNEL} PUBLIC ULTRA_ARCH_PHYS_ADDR_WIDTH=8)

target_sources(
    ${ULTRA_KERNEL}
    PRIVATE
    entry.c
    io.c
    cpuid.c
    hypervisor.c
)
target_include_directories(
    ${ULTRA_KERNEL}
    PRIVATE
    include
)

if (ULTRA_ARCH_EXECUTION_MODE STREQUAL "x86_64")
    ultra_target_compile_options(
        ${ULTRA_KERNEL}
        -mcmodel=kernel
        -mno-red-zone
    )
elseif (NOT ULTRA_TOOLCHAIN_HAS_64BIT_MATH_BUILTINS)
    target_sources(
        ${ULTRA_KERNEL}
        PRIVATE
        builtins.c
    )
endif ()

ultra_target_compile_options(
    ${ULTRA_KERNEL}
    -mno-80387
    -mno-sse
    -mno-mmx
    -mno-sse2
    -mno-3dnow
    -mno-avx
)

set(ULTRA_LD_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/link.ld.template")
set(ULTRA_LD_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/link-${ULTRA_ARCH_EXECUTION_MODE}.ld"
)
add_ultra_ld_template(
    NAME
    kernel-link-script
    TEMPLATE_PATH
    ${ULTRA_LD_TEMPLATE}
    OUT_PATH
    ${ULTRA_LD_SCRIPT}
    DEPENDANT
    ${ULTRA_KERNEL}
)

ultra_target_link_options(
    ${ULTRA_KERNEL}
    -T${ULTRA_LD_SCRIPT}
    -z max-page-size=4096
)

if (ULTRA_TOOLCHAIN STREQUAL "gcc")
    target_link_libraries(
        ${ULTRA_KERNEL}
        PRIVATE
        ${ULTRA_LIBGCC_PATH}
    )
endif()

add_ultra_module(
    NAME
    e9-console
    SOURCES
    e9_console.c
    TYPE
    COMPILED_IN
)
