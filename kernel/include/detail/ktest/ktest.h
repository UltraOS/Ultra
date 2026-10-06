#pragma once

#include <common/types.h>
#include <common/attributes.h>
#include <common/helpers.h>
#include <common/string_container.h>

/*
 * A test that needs a running kernel. Each case runs in a thread of its own and
 * may sleep, take locks and start threads.
 */
struct ktest_case {
    // The module name of the source file that defines the case
    struct string suite;
    struct string name;
    void (*fn)(void);
};

PRINTF_DECL(3, 4)
void ktest_fail(const char *file, u32 line, const char *fmt, ...);

NORETURN
void ktest_abort(void);

enum ktest_value_type : u8 {
    KTEST_VALUE_INTEGER,
    KTEST_VALUE_POINTER,
    KTEST_VALUE_STRING,
};

struct ktest_value {
    enum ktest_value_type type;
    bool is_signed;

    union {
        i64 as_signed;
        u64 as_unsigned;
        const void *as_pointer;
        struct string as_string;
    };
};

static inline struct ktest_value ktest_value_signed(i64 value)
{
    return (struct ktest_value) {
        .type = KTEST_VALUE_INTEGER,
        .is_signed = true,
        .as_signed = value,
    };
}

static inline struct ktest_value ktest_value_unsigned(u64 value)
{
    return (struct ktest_value) {
        .type = KTEST_VALUE_INTEGER,
        .as_unsigned = value,
    };
}

static inline struct ktest_value ktest_value_pointer(const void *value)
{
    return (struct ktest_value) {
        .type = KTEST_VALUE_POINTER,
        .as_pointer = value,
    };
}

static inline struct ktest_value ktest_value_string(struct string value)
{
    return (struct ktest_value) {
        .type = KTEST_VALUE_STRING,
        .as_string = value,
    };
}

int ktest_compare(const struct ktest_value *lhs, const struct ktest_value *rhs);

void ktest_fail_cmp(
    const char *file, u32 line, const char *expr, const struct ktest_value *lhs,
    const struct ktest_value *rhs
);

#define KTEST_VALUE_CTOR(x) _Generic((x),                              \
    bool: ktest_value_unsigned,                                        \
    char: CHOOSE_EXPR(                                                 \
        IS_SIGNED_TYPE(char), ktest_value_signed, ktest_value_unsigned \
    ),                                                                 \
    signed char: ktest_value_signed,                                   \
    short: ktest_value_signed,                                         \
    int: ktest_value_signed,                                           \
    long: ktest_value_signed,                                          \
    long long: ktest_value_signed,                                     \
    unsigned char: ktest_value_unsigned,                               \
    unsigned short: ktest_value_unsigned,                              \
    unsigned int: ktest_value_unsigned,                                \
    unsigned long: ktest_value_unsigned,                               \
    unsigned long long: ktest_value_unsigned,                          \
    struct string: ktest_value_string,                                 \
    default: ktest_value_pointer                                       \
)

#define KTEST_VALUE_TYPE_OF(x) _Generic((x),     \
    struct string: KTEST_VALUE_STRING,           \
    typeof(nullptr): KTEST_VALUE_POINTER,        \
    default: CHOOSE_EXPR(                        \
        CLASSIFY_TYPE(x) == TYPE_CLASS_POINTER,  \
        KTEST_VALUE_POINTER, KTEST_VALUE_INTEGER \
    )                                            \
)

#define KTEST_IS_C_STRING(x)                                        \
    _Generic((x), char *: true, const char *: true, default: false)

#define KTEST_CHECK_OPERANDS(lhs, op, rhs)                              \
    STATIC_ASSERT(                                                      \
        !KTEST_IS_C_STRING(lhs) && !KTEST_IS_C_STRING(rhs),             \
        "C strings must be passed as STR()"                             \
    );                                                                  \
    STATIC_ASSERT(                                                      \
        KTEST_VALUE_TYPE_OF(lhs) == KTEST_VALUE_TYPE_OF(rhs),           \
        "operands must both be integers, both pointers or both strings" \
    );                                                                  \
    STATIC_ASSERT(                                                      \
        KTEST_VALUE_TYPE_OF(lhs) != KTEST_VALUE_STRING ||               \
        __builtin_strcmp(#op, "==") == 0 ||                             \
        __builtin_strcmp(#op, "!=") == 0,                               \
        "strings only support == and !="                                \
    )

#define KTEST_CMP(lhs, op, rhs, lhs_text, rhs_text, on_failure) do {       \
    struct ktest_value ktest_lhs, ktest_rhs;                               \
                                                                           \
    KTEST_CHECK_OPERANDS(lhs, op, rhs);                                    \
                                                                           \
    ktest_lhs = KTEST_VALUE_CTOR(lhs)(lhs);                                \
    ktest_rhs = KTEST_VALUE_CTOR(rhs)(rhs);                                \
                                                                           \
    if (unlikely(!(ktest_compare(&ktest_lhs, &ktest_rhs) op 0))) {         \
        ktest_fail_cmp(                                                    \
            __FILE__, __LINE__, lhs_text " " #op " " rhs_text, &ktest_lhs, \
            &ktest_rhs                                                     \
        );                                                                 \
        on_failure;                                                        \
    }                                                                      \
} while (0)

#define KTEST_CMP_EXPECT(lhs, op, rhs, lhs_text, rhs_text) \
    KTEST_CMP(lhs, op, rhs, lhs_text, rhs_text, )
#define KTEST_CMP_ASSERT(lhs, op, rhs, lhs_text, rhs_text)     \
    KTEST_CMP(lhs, op, rhs, lhs_text, rhs_text, ktest_abort())
