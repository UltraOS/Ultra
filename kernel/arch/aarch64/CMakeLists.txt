ultra_compile_options(-mgeneral-regs-only)

# GCC doesn't support red-zone on aarch64
if (CONFIG_TOOLCHAIN_CLANG)
    ultra_compile_options(-mno-red-zone)
endif()

ultra_sources(
    entry.c
    entry.S
    io.c
    unwind.c
    unwind.S
)
ultra_include_directories(include)

set(ULTRA_LD_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/link.ld.template")
set(ULTRA_LD_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/link-aarch64.ld")
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
