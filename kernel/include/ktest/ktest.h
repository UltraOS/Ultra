#pragma once

#include <common/types.h>
#include <common/attributes.h>
#include <common/helpers.h>

#include <linker.h>

#include <detail/ktest/ktest.h>

#define KTEST_CASE(fn_name)                                           \
    static void fn_name(void);                                        \
    SECTION_VAR(KTEST_CASES_SECTION, static const, struct ktest_case) \
    s_ktest_case_##fn_name = {                                        \
        ULTRA_MODULE_NAME, #fn_name, fn_name,                         \
    };                                                                \
    static void fn_name(void)

NORETURN
void ktest_skip(const char *reason);

#define KTEST_EXPECT(cond) do {                               \
    if (!(cond))                                              \
        ktest_fail(__FILE__, __LINE__, "expected %s", #cond); \
} while (0)

#define KTEST_EXPECT_CMP(lhs, op, rhs) ((void)KTEST_CHECK_CMP(lhs, op, rhs))

#define KTEST_EXPECT_EQ(lhs, rhs) KTEST_EXPECT_CMP(lhs, ==, rhs)
#define KTEST_EXPECT_NE(lhs, rhs) KTEST_EXPECT_CMP(lhs, !=, rhs)
#define KTEST_EXPECT_GE(lhs, rhs) KTEST_EXPECT_CMP(lhs, >=, rhs)
#define KTEST_EXPECT_LE(lhs, rhs) KTEST_EXPECT_CMP(lhs, <=, rhs)

#define KTEST_EXPECT_STREQ(lhs, rhs) ((void)KTEST_CHECK_STREQ(lhs, rhs))

#define KTEST_ASSERT(cond) do {                               \
    if (!(cond)) {                                            \
        ktest_fail(__FILE__, __LINE__, "expected %s", #cond); \
        ktest_abort();                                        \
    }                                                         \
} while (0)

#define KTEST_ASSERT_CMP(lhs, op, rhs) do { \
    if (!KTEST_CHECK_CMP(lhs, op, rhs))     \
        ktest_abort();                      \
} while (0)

#define KTEST_ASSERT_EQ(lhs, rhs) KTEST_ASSERT_CMP(lhs, ==, rhs)
#define KTEST_ASSERT_NE(lhs, rhs) KTEST_ASSERT_CMP(lhs, !=, rhs)
#define KTEST_ASSERT_GE(lhs, rhs) KTEST_ASSERT_CMP(lhs, >=, rhs)
#define KTEST_ASSERT_LE(lhs, rhs) KTEST_ASSERT_CMP(lhs, <=, rhs)

#define KTEST_ASSERT_STREQ(lhs, rhs) do { \
    if (!KTEST_CHECK_STREQ(lhs, rhs))     \
        ktest_abort();                    \
} while (0)
