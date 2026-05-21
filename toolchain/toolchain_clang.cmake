include("${ULTRA_TOOLCHAIN_DIR}/toolchain_defaults.cmake")

if (APPLE)
    execute_process(
        COMMAND brew --prefix lld
        OUTPUT_VARIABLE BREW_LLD_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=${BREW_LLD_PREFIX}/bin/ld.lld")

    execute_process(
        COMMAND brew --prefix llvm
        OUTPUT_VARIABLE BREW_LLVM_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(CMAKE_C_COMPILER "${BREW_LLVM_PREFIX}/bin/clang")
    set(CMAKE_ASM_COMPILER "${BREW_LLVM_PREFIX}/bin/clang")

    # macOS clang's "none-elf"/"none-none" triple falls back to Darwin
    # toolchain selection, injecting -arch/-platform_version/-syslibroot
    # which ld.lld doesn't understand. Use linux-gnu instead — we're
    # freestanding so the OS part doesn't affect anything except linker
    # conventions, which is what we want.
    set(ULTRA_TARGET_TRIPLE "${ULTRA_TARGET_PREFIX}-unknown-linux-gnu")
else ()
    set(CMAKE_C_COMPILER clang)
    set(CMAKE_ASM_COMPILER clang)
    set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
    set(ULTRA_TARGET_TRIPLE "${ULTRA_TARGET_PREFIX}-none-elf")
endif ()

set(CMAKE_C_COMPILER_TARGET "${ULTRA_TARGET_TRIPLE}")
set(CMAKE_ASM_COMPILER_TARGET "${ULTRA_TARGET_TRIPLE}")

set(ULTRA_TOOLCHAIN_LTO_FLAGS "-flto=full")
