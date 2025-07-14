include("${ULTRA_TOOLCHAIN_DIR}/toolchain_defaults.cmake")

if (APPLE)
    execute_process(
        COMMAND
        brew --prefix lld
        OUTPUT_VARIABLE
        BREW_LLD_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(LLD_LINKER "${BREW_LLD_PREFIX}/bin/ld.lld")
else ()
    set(LLD_LINKER "ld.lld")
endif ()

# Do this because cmake attempts to link against -lc and crt0
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --target=${ULTRA_TARGET_PREFIX}-none-none")
set(CMAKE_ASM_FLAGS "${CMAKE_C_FLAGS} --target=${ULTRA_TARGET_PREFIX}-none-none")
set(CMAKE_C_COMPILER clang)
set(CMAKE_ASM_COMPILER clang)
set(CMAKE_LINKER ${LLD_LINKER})
