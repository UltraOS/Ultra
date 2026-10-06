ultra_sources(
    task.c
    core.c
    rr.c
    idle.c
    kthread.c
    wait_queue.c
    completion.c
)

ultra_sources_if(
    CONFIG_KTEST_SCHED
    tests/sched.c
)
