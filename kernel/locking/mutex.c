#include <locking/mutex.h>
#include <preempt.h>
#include <bug.h>

#include <common/atomic.h>
#include <common/bit.h>

#include <sched/task.h>
#include <sched/sched.h>
#include <private/sched/sched.h>

/*
 * The owner word holds a pointer to the owning task, with these flags stored
 * in its low bits.
 */
enum mutex_flags : ptr_t {
    // The wait list is not empty, so mutex_unlock() has to wake up a waiter
    MUTEX_FLAG_HAS_WAITERS = BIT_OF_TYPE(ptr_t, 0),
};

#define MUTEX_FLAGS_MASK MUTEX_FLAG_HAS_WAITERS

BUILD_BUG_ON_WITH_MSG(
    alignof(struct task) <= MUTEX_FLAGS_MASK,
    "The mutex flags must fit in the low bits of a task pointer"
);

struct mutex_waiter {
    struct list_link link;
    struct task *task;
};

void mutex_init(struct mutex *mutex)
{
    *mutex = MUTEX_INIT(*mutex);
}

static bool try_lock(struct mutex *mutex)
{
    ptr_t owner, task, flags, me;

    me = (ptr_t)current;
    owner = atomic_load_relaxed(&mutex->owner);

    for (;;) {
        task = owner & ~MUTEX_FLAGS_MASK;
        flags = owner & MUTEX_FLAGS_MASK;

        if (task != 0)
            return false;

        if (atomic_cmpxchg_acquire(&mutex->owner, &owner, me | flags))
            return true;
    }
}

static bool try_lock_fast(struct mutex *mutex)
{
    ptr_t expected = 0;

    return atomic_cmpxchg_acquire(&mutex->owner, &expected, (ptr_t)current);
}

static bool try_unlock_fast(struct mutex *mutex)
{
    ptr_t expected;

    expected = (ptr_t)current;
    return atomic_cmpxchg_release(&mutex->owner, &expected, 0);
}

static void lock_slow(struct mutex *mutex)
{
    struct mutex_waiter waiter;

    waiter.task = current;
    spin_lock(&mutex->wait_lock);

    if (try_lock(mutex)) {
        spin_unlock(&mutex->wait_lock);
        return;
    }

    if (list_is_empty(&mutex->waiters))
        atomic_fetch_or(&mutex->owner, MUTEX_FLAG_HAS_WAITERS, MO_RELAXED);
    list_insert_prev(&mutex->waiters, &waiter.link);

    for (;;) {
        /*
         * We set our state before trying to take the lock, so that an unlock
         * that happens after the attempt finds us on the wait list and makes us
         * runnable again. Otherwise the schedule() below could sleep forever.
         */
        set_current_state(TASK_STATE_UNINTERRUPTIBLE);

        if (try_lock(mutex))
            break;

        spin_unlock(&mutex->wait_lock);
        schedule();
        spin_lock(&mutex->wait_lock);
    }

    set_current_state(TASK_STATE_RUNNING);

    list_remove(&waiter.link);
    if (list_is_empty(&mutex->waiters))
        atomic_fetch_and(&mutex->owner, ~MUTEX_FLAG_HAS_WAITERS, MO_RELAXED);

    spin_unlock(&mutex->wait_lock);
}

void mutex_lock(struct mutex *mutex)
{
    might_sleep();

    if (likely(try_lock_fast(mutex)))
        return;

    lock_slow(mutex);
}

bool mutex_try_lock(struct mutex *mutex)
{
    BUG_ON(in_hard_irq());

    if (likely(try_lock_fast(mutex)))
        return true;

    return try_lock(mutex);
}

static void unlock_slow(struct mutex *mutex)
{
    struct mutex_waiter *waiter;
    struct task *next = nullptr;
    ptr_t owner, flags;

    owner = atomic_load_relaxed(&mutex->owner);

    for (;;) {
        BUG_ON((owner & ~MUTEX_FLAGS_MASK) != (ptr_t)current);

        flags = owner & MUTEX_FLAGS_MASK;
        if (atomic_cmpxchg_release(&mutex->owner, &owner, flags))
            break;
    }

    spin_lock(&mutex->wait_lock);

    if (!list_is_empty(&mutex->waiters)) {
        waiter = list_first_entry(&mutex->waiters, struct mutex_waiter, link);
        next = waiter->task;

        /*
         * The waiter may already be running if an earlier unlock woke it up
         * and another task took the mutex before it did. Once the wait lock
         * is dropped, it can then take the mutex, return from mutex_lock()
         * and exit before we get to wake it up. Holding a reference keeps its
         * task valid until wake_up_task() is done with it.
         */
        task_ref(next);
    }

    spin_unlock(&mutex->wait_lock);

    if (next == nullptr)
        return;

    wake_up_task(next, TASK_STATE_UNINTERRUPTIBLE);
    task_unref(next);
}

void mutex_unlock(struct mutex *mutex)
{
    if (likely(try_unlock_fast(mutex)))
        return;

    unlock_slow(mutex);
}
