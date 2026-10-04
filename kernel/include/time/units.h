#pragma once

#include <common/types.h>

#define HZ 1ull
#define KHZ (HZ * 1000)
#define MHZ (KHZ * 1000)
#define GHZ (MHZ * 1000)

#define MS_PER_SEC 1000ull
#define US_PER_SEC (MS_PER_SEC * 1000)
#define NS_PER_SEC (US_PER_SEC * 1000)
#define PS_PER_SEC (NS_PER_SEC * 1000)
#define FS_PER_SEC (PS_PER_SEC * 1000)

#define US_PER_MS  1000ull
#define NS_PER_MS  (US_PER_MS * 1000)

#define NS_PER_US  1000ull

#define MS_PER_MIN  (60ull * MS_PER_SEC)
#define MS_PER_HOUR (60ull * MS_PER_MIN)
#define MS_PER_DAY  (24ull * MS_PER_HOUR)
#define MS_PER_YEAR (365ull * MS_PER_DAY)

/*
 * A quantity broken down for display: 'value' and 'thousandths' are the
 * integral and fractional parts of the quantity expressed in 'unit'.
 */
struct human_repr {
    u64 value;
    u64 thousandths;
    const char *unit;
};

#define HUMAN_REPR_FMT "%llu.%03llu %s"
#define HUMAN_REPR_ARG(x) (x).value, (x).thousandths, (x).unit

/*
 * Express 'hz' in the largest unit it reaches, up to GHz, e.g. 2995200000
 * becomes { 2, 995, "GHz" }. The fraction is truncated.
 */
void frequency_to_human(u64 hz, struct human_repr *out_human_repr);

/*
 * Express 'ns' in the largest unit it reaches, from ns up to h, e.g.
 * 90000000000 becomes { 1, 500, "min" }. The fraction is truncated.
 */
void duration_to_human(u64 ns, struct human_repr *out_human_repr);
