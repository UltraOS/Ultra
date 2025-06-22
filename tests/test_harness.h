#pragma once

#include <stdint.h>
#include <stddef.h>

struct test_case {
    void (*run)(void);
    const char *name;
};

#ifdef __cplusplus
extern "C" {
#endif

void add_test_case(struct test_case *test, const char *file);

void do_assert_eq(uint64_t lhs, uint64_t rhs, const char *file, size_t line);
#define ASSERT_EQ(lhs, rhs) do_assert_eq(lhs, rhs, __FILE__, __LINE__)

void malloc_phys_range(uint64_t start, uint64_t size);

// Done automatically after each test case
void reset_phys_ranges(void);

uint64_t translate_virt_to_phys(void* virt);
void *translate_phys_to_virt(uint64_t phys);

#ifdef __cplusplus
}
#endif

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

#ifdef _MSC_VER
    #pragma section(".CRT$XCU", read)
    #define TEST_CASE(case_name)                                  \
        DO_MAKE_TEST_CASE(case_name)                              \
                                                                  \
        static void case_name##_init(void);                       \
        __declspec(allocate(".CRT$XCU"))                          \
        void (*case_name##_hook)(void) = case_name##_init;        \
                                                                  \
        __pragma(comment(linker, "/include:" #case_name "_hook")) \
        static void case_name(void)
#else
    #define TEST_CASE(case_name)                                         \
        __attribute__((constructor)) static void case_name##_init(void); \
        DO_MAKE_TEST_CASE(case_name)                                     \
        static void case_name(void)
#endif
