ultra_sources(
    counter_device.c
    event_device.c
    keeper.c
    timer.c
    units.c
)

ultra_sources_if(
    CONFIG_KTEST_TIME
    tests/time.c
)
