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
        STR(ULTRA_MODULE_NAME), STR(#fn_name), fn_name,               \
    };                                                                \
    static void fn_name(void)

// End the case and report it as skipped, printing 'reason' with the result
NORETURN
void ktest_skip(const char *reason);

/*
 * KTEST_EXPECT_*() records a failure and lets the case continue, while
 * KTEST_ASSERT_*() records it and ends the case. An assert that fails in a
 * thread started by the case ends only that thread.
 */
#define KTEST_EXPECT(cond) do {                               \
    if (unlikely(!(cond)))                                    \
        ktest_fail(__FILE__, __LINE__, "expected %s", #cond); \
} while (0)

#define KTEST_EXPECT_EQ(lhs, rhs) KTEST_CMP_EXPECT(lhs, ==, rhs, #lhs, #rhs)
#define KTEST_EXPECT_NE(lhs, rhs) KTEST_CMP_EXPECT(lhs, !=, rhs, #lhs, #rhs)
#define KTEST_EXPECT_LT(lhs, rhs) KTEST_CMP_EXPECT(lhs, <, rhs, #lhs, #rhs)
#define KTEST_EXPECT_LE(lhs, rhs) KTEST_CMP_EXPECT(lhs, <=, rhs, #lhs, #rhs)
#define KTEST_EXPECT_GT(lhs, rhs) KTEST_CMP_EXPECT(lhs, >, rhs, #lhs, #rhs)
#define KTEST_EXPECT_GE(lhs, rhs) KTEST_CMP_EXPECT(lhs, >=, rhs, #lhs, #rhs)

#define KTEST_ASSERT(cond) do {                               \
    if (unlikely(!(cond))) {                                  \
        ktest_fail(__FILE__, __LINE__, "expected %s", #cond); \
        ktest_abort();                                        \
    }                                                         \
} while (0)

#define KTEST_ASSERT_EQ(lhs, rhs) KTEST_CMP_ASSERT(lhs, ==, rhs, #lhs, #rhs)
#define KTEST_ASSERT_NE(lhs, rhs) KTEST_CMP_ASSERT(lhs, !=, rhs, #lhs, #rhs)
#define KTEST_ASSERT_LT(lhs, rhs) KTEST_CMP_ASSERT(lhs, <, rhs, #lhs, #rhs)
#define KTEST_ASSERT_LE(lhs, rhs) KTEST_CMP_ASSERT(lhs, <=, rhs, #lhs, #rhs)
#define KTEST_ASSERT_GT(lhs, rhs) KTEST_CMP_ASSERT(lhs, >, rhs, #lhs, #rhs)
#define KTEST_ASSERT_GE(lhs, rhs) KTEST_CMP_ASSERT(lhs, >=, rhs, #lhs, #rhs)
