#pragma once

#include <arch/registers.h>
#include <common/error.h>

typedef bool (*unwind_cb_t)(
    void *user, ptr_t return_address, bool address_after_call
);

/*
 * Walk all frames of a stack, if no existing register state is specified,
 * unwinding starts at the caller of this function.
 */
error_t unwind_walk(struct registers*, unwind_cb_t callback, void *user);
