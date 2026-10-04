#include <stdlib.h>
#include <string.h>

#include <memory/alloc_behavior.h>

// Counting allocators that test_sched.c substitutes for the kernel's
int g_sched_test_live_tasks, g_sched_test_live_stacks;

void *sched_test_alloc(size_t size, enum alloc_behavior behavior)
{
    void *ptr;

    ptr = malloc(size);
    if (ptr != nullptr && (behavior & ALLOC_ZEROED))
        memset(ptr, 0, size);

    g_sched_test_live_tasks++;
    return ptr;
}

void sched_test_free(void *ptr)
{
    g_sched_test_live_tasks--;
    free(ptr);
}

void *sched_test_valloc(size_t size, enum alloc_behavior behavior)
{
    (void)behavior;

    g_sched_test_live_stacks++;
    return malloc(size);
}

void sched_test_vrelease(void *ptr)
{
    g_sched_test_live_stacks--;
    free(ptr);
}
