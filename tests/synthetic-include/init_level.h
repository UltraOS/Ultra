#pragma once

#include <common/attributes.h>
#include <common/types.h>
#include <common/error.h>

#include <private/init_level.h>

typedef error_t (*init_call_t)(void);

enum init_level : u32 {
    #define INIT_LEVEL(x) INIT_LEVEL_##x,
    INIT_LEVELS
    #undef INIT_LEVEL

    NUM_INIT_LEVELS,
};

#define MAKE_INIT_CALL(func, level, type)                  \
    STATIC_ASSERT(                                         \
        INIT_LEVEL_##level >= 0,                           \
        "Please use one of init_level enumeration values " \
        "(without a prefix)"                               \
    );                                                     \
    static const init_call_t init_call_hook_##func = func

#define INIT_CALL_PRE(level, func) MAKE_INIT_CALL(func, level, pre)
#define INIT_CALL_POST(level, func) MAKE_INIT_CALL(func, level, post)
