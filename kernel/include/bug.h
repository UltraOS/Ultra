#pragma once

#include <common/attributes.h>
#include <panic.h>
#include <log.h>

#define BUG() \
    panic("BUG! At %s() in file %s:%d\n", __func__, __FILE__, __LINE__)

#define BUG_WITH_MSG(msg, ...) \
    panic("BUG! " msg, ##__VA_ARGS__)

#define BUG_ON(expr)        \
    do {                    \
        if (unlikely(expr)) \
            BUG();          \
    } while (0)

#define BUG_ON_WITH_MSG(expr, msg, ...)     \
    do {                                    \
        if (unlikely(expr))                 \
            BUG_WITH_MSG(msg, __VA_ARGS__); \
    } while (0)

#define WARN()                                            \
    pr_warn("WARNING: At %s() in file %s:%d\n", __func__, \
            __FILE__, __LINE__)

#ifndef _MSC_VER

#define WARN_ON_WITH_MSG(expr, msg, ...) ({ \
    bool true_cond = !!((expr));            \
    if (unlikely(true_cond))                \
        pr_warn(msg, __VA_ARGS__);          \
    unlikely(true_cond);                    \
})

#define WARN_ON(expr) ({         \
    bool true_cond = !!((expr)); \
    if (unlikely(true_cond))     \
        WARN();                  \
    unlikely(true_cond);         \
})

#else

#ifndef ULTRA_TEST
#error MSVC is only supported in test mode
#endif

static inline bool WARN_ON_WITH_MSG(bool expr, const char *msg, ...)
{
    if (expr) {
        va_list vlist;

        va_start(vlist, msg);
        vprint(msg, vlist);
        va_end(vlist);
    }

    return expr;
}

#define WARN_ON(expr)                                                    \
    WARN_ON_WITH_MSG(expr, "WARNING: At %s() in file %s:%d\n", __func__, \
                     __FILE__, __LINE__)

#endif
