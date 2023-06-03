if (ULTRA_ARCH STREQUAL "i686")
    set(ULTRA_X86_POSTFIX 32)
else ()
    set(ULTRA_X86_POSTFIX 64)
endif()

target_sources(
    ${ULTRA_KERNEL}
    PRIVATE
    entry.c
)
target_include_directories(
    ${ULTRA_KERNEL}
    PRIVATE
    include
)

if (ULTRA_ARCH STREQUAL "x86_64")
    ultra_target_compile_options(
        ${ULTRA_KERNEL}
        -mcmodel=kernel
        -mno-red-zone
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
set(ULTRA_LD_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/link${ULTRA_X86_POSTFIX}.ld")
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
