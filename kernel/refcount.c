#include <refcount.h>

#include <bug.h>

static void refcount_report(
    const void *ref, u64 old, u64 max, enum refcount_op op
)
{
    const char *what;

    // Already saturated, reported when it got there
    if (old > max)
        return;

    if (old != 0)
        what = "overflow, leaking the object";
    else if (op == REFCOUNT_OP_INC)
        what = "increment on 0, use-after-free";
    else
        what = "underflow, use-after-free";

    WARN_WITH_MSG("refcount %p (%pMEM): %s", ref, ref, what);
}

#define MAKE_REFCOUNT_SATURATE(width)                                   \
    void refcount##width##_saturate(                                    \
        struct refcount##width *ref, u##width old, enum refcount_op op  \
    )                                                                   \
    {                                                                   \
        atomic_store_relaxed(&ref->value, REFCOUNT##width##_SATURATED); \
        refcount_report(ref, old, REFCOUNT##width##_MAX, op);           \
    }

MAKE_REFCOUNT_SATURATE(32)
MAKE_REFCOUNT_SATURATE(64)
