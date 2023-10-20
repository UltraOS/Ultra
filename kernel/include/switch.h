#pragma once

#include <bug.h>

#if ULTRA_ARCH_WIDTH >= 8
#define WIDTH_SWITCH_8            \
    case 8:                       \
        WIDTH_SWITCH_ACTION_8(64)
#else
#define WIDTH_SWITCH_8
#endif

#define WIDTH_SWITCH(width)     \
    switch (width) {            \
    case 1:                     \
        WIDTH_SWITCH_ACTION(8)  \
    case 2:                     \
        WIDTH_SWITCH_ACTION(16) \
    case 4:                     \
        WIDTH_SWITCH_ACTION(32) \
    WIDTH_SWITCH_8              \
    default:                    \
        BUG();                  \
    }
