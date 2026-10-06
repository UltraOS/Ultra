ultra_sources(
    spinlock.c
    mutex.c
)

ultra_sources_if(
    CONFIG_KTEST_MUTEX
    tests/mutex.c
)
