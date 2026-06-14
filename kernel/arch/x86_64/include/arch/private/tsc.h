#pragma once

#include <common/types.h>

#include <time/counter_device.h>

#define TSC_APPROXIMATE_RATING (COUNTER_DEVICE_RATING_IDEAL + 1)
#define TSC_PRECISE_RATING (TSC_APPROXIMATE_RATING + 2)

void tsc_set_known_frequency(u64 hz, const char *reported_by);
