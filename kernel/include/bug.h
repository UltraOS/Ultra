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

#define BUG_ON_WITH_MSG(expr, msg, ...)       \
    do {                                      \
        if (unlikely(expr))                   \
            BUG_WITH_MSG(msg, ##__VA_ARGS__); \
    } while (0)

#ifdef ULTRA_DEADLY_WARNINGS
#define DIE_IF_DEADLY_WARNINGS() panic("Warnings are configured as deadly")
#else
#define DIE_IF_DEADLY_WARNINGS()
#endif

#define WARN() do {                                           \
        pr_warn("WARNING: At %s() in file %s:%d\n", __func__, \
                __FILE__, __LINE__);                          \
        DIE_IF_DEADLY_WARNINGS();                             \
    } while (0)

#define WARN_ON_WITH_MSG(expr, msg, ...) ({ \
    bool true_cond = !!((expr));            \
    if (unlikely(true_cond)) {              \
        pr_warn(msg, ##__VA_ARGS__);        \
        DIE_IF_DEADLY_WARNINGS();           \
    }                                       \
    unlikely(true_cond);                    \
})

#define WARN_ON(expr) ({         \
    bool true_cond = !!((expr)); \
    if (unlikely(true_cond))     \
        WARN();                  \
    unlikely(true_cond);         \
})
