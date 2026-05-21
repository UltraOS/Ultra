# Sane defaults that should work for most non-quirky toolchains

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ${ULTRA_ARCH})
set(CMAKE_SYSROOT "")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

function(ultra_target_compile_options TARGET)
    target_compile_options(
        ${TARGET}
        ${ARGN}
    )

    if (CONFIG_LTO)
        ultra_target_link_options(${ARGV})
    endif ()
endfunction()

function(ultra_target_link_options TARGET)
    target_link_options(
        ${TARGET}
        ${ARGN}
    )
endfunction()

set(ULTRA_LANGUAGES "C;ASM")

set(ULTRA_TARGET_PREFIX ${ULTRA_ARCH})
