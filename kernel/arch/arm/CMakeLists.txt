if (ULTRA_ARCH_EXECUTION_MODE STREQUAL "aarch32")
    target_compile_definitions(${ULTRA_KERNEL} PUBLIC ULTRA_ARCH_WIDTH=4)
    target_compile_definitions(${ULTRA_KERNEL} PUBLIC ULTRA_PHYS_ADDR_WIDTH=4)
    ultra_target_compile_options(
        ${ULTRA_KERNEL}
        -mfloat-abi=soft
    )
    message(
        FATAL_ERROR
        "aarch32 is not implemented (TODO: soft math helpers, loader support)"
    )
else ()
    target_compile_definitions(${ULTRA_KERNEL} PUBLIC ULTRA_ARCH_WIDTH=8)
    target_compile_definitions(${ULTRA_KERNEL} PUBLIC ULTRA_PHYS_ADDR_WIDTH=8)
    ultra_target_compile_options(
        ${ULTRA_KERNEL}
        -mgeneral-regs-only
    )

    # GCC doesn't support red-zone on aarch64
    if (ULTRA_TOOLCHAIN STREQUAL "clang")
        ultra_target_compile_options(
            ${ULTRA_KERNEL}
            PRIVATE
            -mno-red-zone
        )
    endif()
endif()

target_sources(
    ${ULTRA_KERNEL}
    PRIVATE
    entry.c
    io.c
)
target_include_directories(
    ${ULTRA_KERNEL}
    PRIVATE
    include
)

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
    ${ULTRA_KERNEL}
)

ultra_target_link_options(
    ${ULTRA_KERNEL}
    -T${ULTRA_LD_SCRIPT}
    -z max-page-size=4096
)
