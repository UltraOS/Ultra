#pragma once

#include <common/helpers.h>
#include <common/string_container.h>

#include <free_after_init.h>

/*
 * Dispatch on the address so that a char array is seen as such along with
 * its size, a plain char pointer intentionally matches nothing.
 */
#define PARAM_TYPE_OPS(value) _Generic(&(value), \
    char (*)[sizeof(value)]: g_param_string_ops, \
    i8*: g_param_i8_ops,                         \
    u8*: g_param_u8_ops,                         \
    i16*: g_param_i16_ops,                       \
    u16*: g_param_u16_ops,                       \
    i32*: g_param_i32_ops,                       \
    u32*: g_param_u32_ops,                       \
    i64*: g_param_i64_ops,                       \
    u64*: g_param_u64_ops,                       \
    bool*: g_param_bool_ops                      \
)

#define PARAM_CAPACITY(value) _Generic(&(value), \
    char (*)[sizeof(value)]: sizeof(value),      \
    default: 0                                   \
)

#define SUBOPTION_VARIANT_TABLE(...) \
    CONCAT(SUBOPTION_VARIANT_TABLE_, GET_NUM_ARGS(__VA_ARGS__))(__VA_ARGS__)
#define SUBOPTION_VARIANT_TABLE_0() nullptr, 0
#define SUBOPTION_VARIANT_TABLE_1(table)                 \
    (table), ARRAY_SIZE(table) + EMBED_STATIC_ASSERT(    \
        !ARE_SAME_TYPE(table, &(table)[0]),              \
        "a table given without a count must be an array" \
    )
#define SUBOPTION_VARIANT_TABLE_2(table, count) (table), (count)

// The name of a parameter is that of its variable minus a g_ or s_ prefix
#define PARAM_NAME(name)                                        \
    __builtin_choose_expr(                                      \
        __builtin_strncmp(#name, "g_", 2) == 0 ||               \
        __builtin_strncmp(#name, "s_", 2) == 0,                 \
        (struct string) { { &(#name)[2] }, sizeof(#name) - 3 }, \
        STR_CONSTEXPR(#name)                                    \
    )

#define PARAM_ENTRY(name, value, ops, flags) \
    { PARAM_NAME(name), &(ops), { &(value), PARAM_CAPACITY(value) }, (flags) }

#define PARAM_ACTION_OPS(name, fn, placement)                        \
    static const struct param_ops placement s_param_##name##_ops = { \
        .allows_empty_value = true,                                  \
        .set = (fn),                                                 \
    }

#define PARAM_ACTION_ENTRY(name, flags) \
    { PARAM_NAME(name), &s_param_##name##_ops, { nullptr, 0 }, (flags) }

#define PARAM_TYPE_SET(value) _Generic(&(value), \
    char (*)[sizeof(value)]: param_set_string,   \
    i8*: param_set_i8,                           \
    u8*: param_set_u8,                           \
    i16*: param_set_i16,                         \
    u16*: param_set_u16,                         \
    i32*: param_set_i32,                         \
    u32*: param_set_u32,                         \
    i64*: param_set_i64,                         \
    u64*: param_set_u64,                         \
    bool*: param_set_bool                        \
)

#define PARAM_TYPE_SUBOPTION_FLAGS(value) _Generic(&(value), \
    bool*: SUBOPTION_ALLOWS_EMPTY_VALUE,                     \
    default: SUBOPTION_FLAG_NONE                             \
)

#define SUBOPTION_ENTRY(name, value, set, flags) \
    { PARAM_NAME(name), (set), { &(value), PARAM_CAPACITY(value) }, (flags) }
