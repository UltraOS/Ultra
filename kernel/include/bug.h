#pragma once

#include <common/attributes.h>
#include <common/helpers.h>
#include <common/types.h>
#include <log.h>

#if HAS_INCLUDE(<arch/bug.h>)
#include <arch/bug.h>
#endif

#ifndef BUG
NORETURN
void bug_report(const char *file, u32 line);

#define BUG() bug_report(__FILE__, __LINE__)
#endif

#ifndef WARN
void warn_report(const char *file, u32 line);

#define WARN() warn_report(__FILE__, __LINE__)
#endif

#define BUG_WITH_MSG(msg, ...) do {        \
        pr_emerg(msg "\n", ##__VA_ARGS__); \
        BUG();                             \
    } while (0)

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

#define WARN_WITH_MSG(msg, ...) do {      \
        pr_warn(msg "\n", ##__VA_ARGS__); \
        WARN();                           \
    } while (0)

#define WARN_ON_WITH_MSG(expr, msg, ...) ({ \
    bool true_cond = !!((expr));            \
    if (unlikely(true_cond))                \
        WARN_WITH_MSG(msg, ##__VA_ARGS__);  \
    unlikely(true_cond);                    \
})

#define WARN_ON(expr) ({         \
    bool true_cond = !!((expr)); \
    if (unlikely(true_cond))     \
        WARN();                  \
    unlikely(true_cond);         \
})
