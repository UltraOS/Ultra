ultra_sources_if(
    CONFIG_KTEST
    ktest.c
    select.c
)

ultra_sources_if(
    CONFIG_KTEST_SELFTEST
    selftest.c
)
