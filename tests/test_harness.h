#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

struct test_case {
    void (*run)(void);
    const char *name;
};

#ifdef __cplusplus
extern "C" {
#endif

void add_test_case(struct test_case *test, const char *file);
void teardown_callback_register(void (*callback)(void), const char *file);

void do_assert_eq(uint64_t lhs, uint64_t rhs, const char *file, size_t line);
void do_assert_ne(uint64_t lhs, uint64_t rhs, const char *file, size_t line);

void do_assert_str_eq(const char *lhs, const char *rhs,
                      const char *file, size_t line);

#define ASSERT(val) do_assert_eq(!!(val), 1, __FILE__, __LINE__)
#define ASSERT_EQ(lhs, rhs) \
    do_assert_eq((uint64_t)lhs, (uint64_t)rhs, __FILE__, __LINE__)
#define ASSERT_NE(lhs, rhs) \
    do_assert_ne((uint64_t)lhs, (uint64_t)rhs, __FILE__, __LINE__)
#define ASSERT_STR_EQ(lhs, rhs) do_assert_str_eq(lhs, rhs, __FILE__, __LINE__)

#define ASSERT_TRUE(val)  ASSERT(val)
#define ASSERT_FALSE(val) do_assert_eq(!!(val), 0, __FILE__, __LINE__)

void malloc_phys_range(uint64_t start, uint64_t size);

// Done automatically after each test case
void reset_phys_ranges(void);

uint64_t translate_virt_to_phys(void* virt);
void *translate_phys_to_virt(uint64_t phys);

uint64_t ns_timer(void);

#ifdef __cplusplus
}
#endif

#define TEST_TEARDOWN()                                      \
    static void test_teardown(void);                         \
                                                             \
    __attribute__((constructor))                             \
    static void test_teardown_register(void)                 \
    {                                                        \
        teardown_callback_register(test_teardown, __FILE__); \
    }                                                        \
    static void test_teardown(void)

#define DO_MAKE_TEST_CASE(case_name)                \
    static void case_name(void);                    \
                                                    \
    static struct test_case test_##case_name = {    \
        .run = case_name,                           \
        .name = #case_name                          \
    };                                              \
    static void case_name##_init(void)              \
    {                                               \
        add_test_case(&test_##case_name, __FILE__); \
    }

#define TEST_CASE(case_name)                    \
    __attribute__((constructor))                \
    static void CONCAT(case_name, _init)(void); \
    DO_MAKE_TEST_CASE(case_name)                \
    static void case_name(void)
