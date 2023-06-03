# Dummy stubs, don't actually set any flags to prevent CMake errors when it's
# invoked purely for the sake of syntax highlighting working properly.
# Otherwise this breaks with any other toolchain besides GCC & clang (e.g MSVC)

function(ultra_target_compile_options TARGET)
endfunction()

function(ultra_target_link_options TARGET)
endfunction()

set(ULTRA_LANGUAGES "C")
