#pragma once

#include <common/error.h>
#include <cpu_mask.h>

/*
 * Allocate a vector on the least busy online CPU, optionally filtered
 * by 'allowed' (NULL means any online CPU).
 */
error_t vector_alloc(
    const struct cpu_mask *allowed, u32 *out_cpu, u8 *out_vector
);

void vector_free(u32 cpu, u8 vector);
