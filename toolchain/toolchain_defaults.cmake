# Sane defaults that should work for most non-quirky toolchains

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR ${ULTRA_ARCH})
set(CMAKE_SYSROOT "")

set(CMAKE_C_LINK_EXECUTABLE
    "<CMAKE_LINKER> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> \
     <OBJECTS> -o <TARGET> <LINK_LIBRARIES>"
)

function(ultra_target_compile_options TARGET)
    target_compile_options(
        ${TARGET}
        PRIVATE
        ${ARGN}
    )
endfunction()

function(ultra_target_link_options TARGET)
    target_link_options(
        ${TARGET}
        PRIVATE
        ${ARGN}
    )
endfunction()

set(ULTRA_LANGUAGES "C;ASM")

set(ULTRA_TARGET_PREFIX ${ULTRA_ARCH_EXECUTION_MODE})
if (ULTRA_TARGET_PREFIX STREQUAL "aarch32")
    set(ULTRA_TARGET_PREFIX "arm")
endif ()
