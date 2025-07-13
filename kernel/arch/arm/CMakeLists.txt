if (ULTRA_ARCH_EXECUTION_MODE STREQUAL "aarch32")
    ultra_compile_definitions(ULTRA_ARCH_WIDTH=4)
    ultra_compile_definitions(ULTRA_PHYS_ADDR_WIDTH=4)
    ultra_compile_options(-mfloat-abi=soft)
    message(
        FATAL_ERROR
        "aarch32 is not implemented (TODO: soft math helpers, loader support)"
    )
else ()
    ultra_compile_definitions(ULTRA_ARCH_WIDTH=8)
    ultra_compile_definitions(ULTRA_PHYS_ADDR_WIDTH=8)
    ultra_compile_options(-mgeneral-regs-only)

    # GCC doesn't support red-zone on aarch64
    if (ULTRA_TOOLCHAIN STREQUAL "clang")
        ultra_compile_options(-mno-red-zone)
    endif()
endif()

ultra_sources(
    entry.c
    io.c
)
ultra_include_directories(include)

set(ULTRA_LD_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/link.ld.template")
set(ULTRA_LD_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/link-${ULTRA_ARCH_EXECUTION_MODE}.ld")
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
