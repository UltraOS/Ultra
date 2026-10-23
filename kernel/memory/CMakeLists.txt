ultra_sources(
    units.c
    init.c
    alloc.c
    boot_alloc.c
    io.c
    page_table.c
    buddy.c
    valloc.c
    describe.c
)

ultra_sources_if(
    CONFIG_KTEST_MEMORY_DESCRIBE
    tests/describe.c
)
