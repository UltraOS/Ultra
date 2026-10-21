#pragma once

#include <common/error.h>
#include <cpu_mask.h>

/*
 * Allocate a vector on the least busy online CPU, optionally filtered
 * by 'allowed' (NULL means any online CPU).
 */
error_t vector_alloc(
    const struct cpu_mask *allowed, cpu_t *out_cpu, u8 *out_vector
);

void vector_free(cpu_t cpu, u8 vector);
