ultra_sources_if(
    CONFIG_KTEST
    helpers.c
    ktest.c
)

ultra_sources_if(
    CONFIG_KTEST_SELFTEST
    selftest.c
)
