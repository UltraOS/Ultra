include(CheckCompilerFlag)

if (ULTRA_ARCH_EXECUTION_MODE STREQUAL "i686")
    ultra_compile_definitions(ULTRA_ARCH_WIDTH=4)
else ()
    ultra_compile_definitions(ULTRA_ARCH_WIDTH=8)
endif()

# Hardcode 8 for now, even for i686 (unconditionally enable PAE if it's present)
ultra_compile_definitions(ULTRA_ARCH_PHYS_ADDR_WIDTH=8)

ultra_sources(
    entry.c
    io.c
    cpuid.c
    hypervisor.c
    memory.c
    unwind.c
    unwind.S
    arch_helpers.S
    interrupts.S
    idt.c
    irq.c
    exceptions.c
    earlycon.c
)
ultra_include_directories(include)

check_compiler_flag(
    C -mpreferred-stack-boundary=4
    HAVE_PREFERRED_STACK_BOUNDARY
)
check_compiler_flag(
    C -mstack-alignment=16
    HAVE_STACK_ALIGNMENT
)

if (HAVE_PREFERRED_STACK_BOUNDARY)
    ultra_compile_options(-mpreferred-stack-boundary=3)
elseif (HAVE_STACK_ALIGNMENT)
    ultra_compile_options(-mstack-alignment=8)
else ()
    message(FATAL_ERROR "This compiler doesn't support setting stack alignment")
endif()

if (ULTRA_ARCH_EXECUTION_MODE STREQUAL "x86_64")
    ultra_compile_options(
        -mcmodel=kernel
        -mno-red-zone
    )
elseif (NOT ULTRA_TOOLCHAIN_HAS_64BIT_MATH_BUILTINS)
    ultra_sources(
        builtins.c
    )
endif ()

ultra_compile_options(
    -mno-80387
    -mno-sse
    -mno-mmx
    -mno-sse2
    -mno-3dnow
    -mno-avx
    -mskip-rax-setup
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
    ${ULTRA_KERNEL_TARGETS}
)

ultra_link_options(
    -T${ULTRA_LD_SCRIPT}
    -z max-page-size=4096
)

if (ULTRA_TOOLCHAIN STREQUAL "gcc")
    ultra_link_libraries(${ULTRA_LIBGCC_PATH})
endif()
