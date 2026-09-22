add_ultra_module(
    NAME flanterm_console
    CONFIG FLANTERM_CONSOLE
    SUBMODULES flanterm
    SOURCES
        console.c
        flanterm/src/flanterm.c
        flanterm/src/flanterm_backends/fb.c
    PRIVATE_INCLUDE_DIRS
        flanterm/src
    PRIVATE_CFLAGS
        -Wno-implicit-fallthrough
    PRIVATE_DEFINITIONS
        FLANTERM_FB_DISABLE_BUMP_ALLOC
)
