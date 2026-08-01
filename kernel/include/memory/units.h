#pragma once

#include <common/types.h>

#define KiB 1024ull
#define MiB (KiB * 1024)
#define GiB (MiB * 1024)
#define TiB (GiB * 1024)
#define PiB (TiB * 1024)

/*
 * A byte count broken down for display: 'value' and 'hundredths' are the
 * integral and fractional parts of the size expressed in 'unit'.
 */
struct human_size {
    size_t value;
    size_t hundredths;
    const char *unit;
};

/*
 * Express 'size' in the largest unit it reaches, e.g. 2140758016 becomes
 * { 1, 99, "GiB" }. The fraction is truncated rather than rounded, so the
 * result never claims more than the caller actually has.
 */
void size_to_human(size_t size, struct human_size *out_human_size);

/*
 * Same as size_to_human, except this uses one char strings for the unit
 * representation, e.g. B/K/M/G/T instead of B/KiB/MiB/GiB/TiB etc.
 */
void size_to_human_short(size_t size, struct human_size *out_human_size);
