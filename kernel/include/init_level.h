#pragma once

#include <common/attributes.h>
#include <common/types.h>
#include <common/error.h>

#include <private/init_level.h>

typedef error_t (*init_call_t)(void);

#ifndef ULTRA_RUNTIME_MODULE

#define MAKE_INIT_CALL(func, level, type)                  \
    STATIC_ASSERT(                                         \
        INIT_LEVEL_##level >= 0,                           \
        "Please use one of init_level enumeration values " \
        "(without a prefix)"                               \
    );                                                     \
    SECTION_VAR(                                           \
        INIT_LEVEL_CB_SECTION(level, type),                \
        static const, init_call_t                          \
    )                                                      \
    init_call_hook_##func = func

#define INIT_CALL_PRE(level, func) MAKE_INIT_CALL(func, level, pre)
#define INIT_CALL_POST(level, func) MAKE_INIT_CALL(func, level, post)

#endif

enum init_level : u32 {
    #define INIT_LEVEL(x) INIT_LEVEL_##x,
    INIT_LEVELS
    #undef INIT_LEVEL

    NUM_INIT_LEVELS,
};
enum init_level init_level(void);
bool init_level_at_least(enum init_level);
bool init_level_below(enum init_level);

void init_level_raise(enum init_level next_level);

/*
 * Queue an init level raise from an init call. This may only
 * be done in an INIT_CALL_POST type of callback.
 */
void init_level_raise_deferred(enum init_level next_level);
