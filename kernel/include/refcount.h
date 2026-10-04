#pragma once

#include <detail/refcount.h>

/*
 * An opaque 32/64 bit structure that represents a reference count.
 *
 * Use refcount_* helpers to manipulate, all fields are considered internal.
 */
struct refcount32;
struct refcount64;

#define REFCOUNT_INIT(n) { .value = (n) }

#define refcount_set(ref, value) REFCOUNT_DISPATCH(ref, set)(ref, value)
#define refcount_read(ref) REFCOUNT_DISPATCH(ref, read)(ref)
#define refcount_inc(ref) REFCOUNT_DISPATCH(ref, inc)(ref)
#define refcount_dec_and_test(ref) REFCOUNT_DISPATCH(ref, dec_and_test)(ref)
