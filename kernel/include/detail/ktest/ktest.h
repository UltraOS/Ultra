#pragma once

#include <common/types.h>
#include <common/attributes.h>
#include <common/string.h>

struct ktest_case {
    const char *suite;
    const char *name;
    void (*fn)(void);
};

PRINTF_DECL(3, 4)
void ktest_fail(const char *file, u32 line, const char *fmt, ...);

void ktest_fail_cmp(
    const char *file, u32 line, const char *expr, u64 lhs, u64 rhs,
    bool is_signed
);

NORETURN
void ktest_abort(void);

#define KTEST_IS_SIGNED(value) _Generic((value), \
    int: true,                                   \
    long: true,                                  \
    long long: true,                             \
    default: false                               \
)

#define KTEST_CHECK_CMP(lhs, op, rhs) ({                               \
    typeof((lhs) + (rhs)) ktest_lhs, ktest_rhs;                        \
    bool ktest_held;                                                   \
                                                                       \
    ktest_lhs = (lhs);                                                 \
    ktest_rhs = (rhs);                                                 \
    ktest_held = ktest_lhs op ktest_rhs;                               \
                                                                       \
    if (!ktest_held) {                                                 \
        ktest_fail_cmp(                                                \
            __FILE__, __LINE__, #lhs " " #op " " #rhs,                 \
            (u64)ktest_lhs, (u64)ktest_rhs, KTEST_IS_SIGNED(ktest_lhs) \
        );                                                             \
    }                                                                  \
                                                                       \
    ktest_held;                                                        \
})

#define KTEST_CHECK_STREQ(lhs, rhs) ({                                \
    const char *ktest_lhs, *ktest_rhs;                                \
    bool ktest_held;                                                  \
                                                                      \
    ktest_lhs = (lhs);                                                \
    ktest_rhs = (rhs);                                                \
    ktest_held = strcmp(ktest_lhs, ktest_rhs) == 0;                   \
                                                                      \
    if (!ktest_held) {                                                \
        ktest_fail(                                                   \
            __FILE__, __LINE__, "expected %s, got \"%s\" and \"%s\"", \
            #lhs " == " #rhs, ktest_lhs, ktest_rhs                    \
        );                                                            \
    }                                                                 \
                                                                      \
    ktest_held;                                                       \
})
