#pragma once

enum memory_order {
    MO_RELAXED = __ATOMIC_RELAXED,
    MO_CONSUME = __ATOMIC_CONSUME,
    MO_ACQ_REL = __ATOMIC_ACQ_REL,
    MO_ACQUIRE = __ATOMIC_ACQUIRE,
    MO_RELEASE = __ATOMIC_RELEASE,
    MO_SEQ_CST = __ATOMIC_SEQ_CST,
};

/*
 * Prevents the compiler from reordering code around the barrier, has no effect
 * on CPU reordering.
 */
#define compiler_barrier() __atomic_signal_fence(MO_ACQ_REL)

/*
 * Makes all past atomic loads acquire-loads, no future loads/stores
 * can be reordered before the last atomic load.
 */
#define barrier_acquire() \
    ({ compiler_barrier(); __atomic_thread_fence(MO_ACQUIRE); })

/*
 * Makes all following atomic stores release-stores, no previous
 * loads/stores can be reordered after the first atomic store.
 */
#define barrier_release() \
    ({ compiler_barrier(); __atomic_thread_fence(MO_RELEASE); })

/*
 * A stronger combination of the above barriers, makes all surrounding
 * atomic operations sequentially consistent.
 */
#define barrier_full() \
    ({ compiler_barrier(); __atomic_thread_fence(MO_SEQ_CST); })

#define atomic_load_explicit(ptr, mo) __atomic_load_n(ptr, mo)
#define atomic_load_relaxed(ptr) atomic_load_explicit(ptr, MO_RELAXED)
#define atomic_load_acquire(ptr) atomic_load_explicit(ptr, MO_ACQUIRE)
#define atomic_load_seq_cst(ptr) atomic_load_explicit(ptr, MO_SEQ_CST)

#define atomic_store_explicit(ptr, x, mo) __atomic_store_n(ptr, x, mo)
#define atomic_store_relaxed(ptr, x) atomic_store_explicit(ptr, x, MO_RELAXED)
#define atomic_store_release(ptr, x) atomic_store_explicit(ptr, x, MO_RELEASE)
#define atomic_store_seq_cst(ptr, x) atomic_store_explicit(ptr, x, MO_SEQ_CST)

#define atomic_add_fetch(ptr, x, mo) __atomic_add_fetch(ptr, x, mo)
#define atomic_sub_fetch(ptr, x, mo) __atomic_sub_fetch(ptr, x, mo)
#define atomic_and_fetch(ptr, x, mo) __atomic_and_fetch(ptr, x, mo)
#define atomic_or_fetch(ptr, x, mo)  __atomic_or_fetch(ptr, x, mo)
#define atomic_xor_fetch(ptr, x, mo) __atomic_xor_fetch(ptr, x, mo)
#define atomic_xchg(ptr, x, mo) __atomic_exchange_n(ptr, x, mo)

#define atomic_cmpxchg_explicit(ptr, expected, desired, success_mo, fail_mo) \
    __atomic_compare_exchange_n(ptr, &expected, desired, 0, success_mo, fail_mo)

#define atomic_cmpxchg_acq_rel(ptr, expected, desired) \
    atomic_cmpxchg_explicit(ptr, expected, desired, MO_ACQ_REL, MO_ACQUIRE)
