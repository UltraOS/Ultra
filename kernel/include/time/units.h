#pragma once

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
