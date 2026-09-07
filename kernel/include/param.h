#pragma once

#include <common/types.h>
#include <common/helpers.h>
#include <common/string_container.h>
#include <common/error.h>

#include <linker.h>
#include <common/bit.h>

enum param_flags {
    /*
     * The value may be changed on a running system, the setter is invoked
     * with is_runtime set to true for such writes. Writing is always a
     * privileged operation.
     */
    PARAM_RUNTIME_WRITABLE = 1 << 0,

    /*
     * Only privileged readers may see the value.
     */
    PARAM_PRIVILEGED_READ = 1 << 1,
};

struct param_value {
    void *ptr;

    // Size in bytes for values stored in a char array
    size_t capacity;
};

/*
 * Sets the value to one specified by the string. Returns an error in case
 * the operation wasn't successful.
 *
 * The string is only valid for the duration of the call. is_runtime is
 * false when the value comes from the kernel command line at boot and
 * true when it is being modified on a running system.
 */
typedef error_t (*param_set_t)(
    struct string, struct param_value*, bool is_runtime
);

/*
 * Converts the value to a null-terminated string.
 *
 * The string size is the capacity of the buffer on entry and the number
 * of bytes written not including the terminating null on return. The
 * return value is the number of bytes required not including the
 * terminating null. If the required bytes don't fit along with the
 * terminating null, the buffer contents are unspecified.
 */
typedef size_t (*param_get_t)(struct string*, const struct param_value*);

struct param {
    struct string name;
    const struct param_ops *ops;

    // TODO: reference to the module defining this parameter

    struct param_value value;
    u32 flags;
};

struct param_ops {
    bool allows_empty_value;
    param_set_t set;

    /*
     * May be NULL for a parameter that cannot be read back, such a parameter
     * is never exposed to readers.
     */
    param_get_t get;
};

// Helper for get callbacks that produce a string
size_t param_write_string(struct string *out, struct string value);

#define PARAMETER_OPS_DECL(type)                                        \
    error_t param_set_##type(struct string, struct param_value*, bool); \
    size_t param_get_##type(struct string*, const struct param_value*); \
                                                                        \
    extern const struct param_ops g_param_##type##_ops;

PARAMETER_OPS_DECL(i8);
PARAMETER_OPS_DECL(u8);
PARAMETER_OPS_DECL(i16);
PARAMETER_OPS_DECL(u16);
PARAMETER_OPS_DECL(i32);
PARAMETER_OPS_DECL(u32);
PARAMETER_OPS_DECL(i64);
PARAMETER_OPS_DECL(u64);
PARAMETER_OPS_DECL(bool)
PARAMETER_OPS_DECL(string)

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

/*
 * Parameters are set from the kernel command line as soon as it is available
 * at boot and can be read back if their ops provide a get callback. See enum
 * param_flags for what a parameter may additionally opt into.
 *
 * A setter that cannot act on its value that early caches it and acts at the
 * init level where it can.
 *
 * custom_parameter is the fully explicit form, the shorthands deduce the
 * name from the variable, the ops from its type, or both.
 */
#define PARAM_ENTRY(name, value, ops, flags) \
    { PARAM_NAME(name), &(ops), { &(value), PARAM_CAPACITY(value) }, (flags) }

#define custom_parameter(name, value, ops, flags)               \
    SECTION_VAR(PARAMETERS_SECTION, static const, struct param) \
    s_param_##name = PARAM_ENTRY(name, value, ops, flags)

#define renamed_parameter_with_ops(name, var, ops) \
    custom_parameter(name, var, ops, 0)

#define renamed_parameter_with_flags(name, var, flags)      \
    custom_parameter(name, var, PARAM_TYPE_OPS(var), flags)
#define renamed_parameter(name, var) renamed_parameter_with_flags(name, var, 0)

#define parameter_with_ops_and_flags(var, ops, flags) \
    custom_parameter(var, var, ops, flags)
#define parameter_with_ops(var, ops) parameter_with_ops_and_flags(var, ops, 0)

#define parameter_with_flags(var, flags)          \
    renamed_parameter_with_flags(var, var, flags)
#define parameter(var) parameter_with_flags(var, 0)

/*
 * Action parameters have no stored value, fn is called with whatever value
 * the parameter is given, including an empty one, and the parameter is never
 * exposed to readers. fn has the signature of the set callback.
 */
#define param_action_ops(name, fn)                         \
    static const struct param_ops s_param_##name##_ops = { \
        .allows_empty_value = true,                        \
        .set = (fn),                                       \
    }

#define PARAM_ACTION_ENTRY(name, flags) \
    { PARAM_NAME(name), &s_param_##name##_ops, { nullptr, 0 }, (flags) }

#define action_parameter_with_flags(name, fn, flags)            \
    param_action_ops(name, fn);                                 \
    SECTION_VAR(PARAMETERS_SECTION, static const, struct param) \
    s_param_##name = PARAM_ACTION_ENTRY(name, flags)
#define action_parameter(name, fn) action_parameter_with_flags(name, fn, 0)

enum suboption_flags : u32 {
    SUBOPTION_FLAG_NONE = 0,

    // A bare key with no value is accepted, the setter sees an empty string
    SUBOPTION_ALLOWS_EMPTY_VALUE = BIT_U32(0),
};

// A sub-option of a parameter value, e.g. the "colored" in earlycon=e9,colored
struct suboption {
    struct string name;
    param_set_t set;
    struct param_value value;
    enum suboption_flags flags;
};

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

#define custom_suboption(name, value, set, flags) \
    SUBOPTION_ENTRY(name, value, set, flags)

#define renamed_suboption_with_flags(name, var, flags) \
    custom_suboption(                                  \
        name, var, PARAM_TYPE_SET(var),                \
        PARAM_TYPE_SUBOPTION_FLAGS(var) | (flags)      \
    )
#define renamed_suboption(name, var) \
    renamed_suboption_with_flags(name, var, SUBOPTION_FLAG_NONE)

#define suboption_with_flags(var, flags) \
    renamed_suboption_with_flags(var, var, flags)
#define suboption(var) renamed_suboption(var, var)

// An action sub-option has no value, fn is called with whatever it is given
#define action_suboption(name, fn) \
    { PARAM_NAME(name), (fn), { nullptr, 0 }, SUBOPTION_ALLOWS_EMPTY_VALUE }

struct param_table {
    struct param *params;
    size_t count;
};

/*
 * Parses the given command line against the given parameter tables, searched
 * in order, and returns the string (if any) after --. Unknown parameters and
 * bad values are logged. cmdline_parse is the single table form.
 */
struct string cmdline_parse_tables(
    struct string cmdline, const struct param_table *tables, size_t num_tables
);
struct string cmdline_parse(
    struct string cmdline, struct param *params, size_t num_params
);

/*
 * Parses the sub-options of a parameter value, a list of key[=value] entries
 * separated by commas or the given separator, against the given table. An
 * unknown key, an empty entry or a bad value is an error. Entries before the
 * failing one have already been applied. Failures are logged at boot only.
 */
error_t parse_suboptions_with_separator(
    struct string list, char separator, struct suboption *opts,
    size_t num_opts, bool is_runtime
);
error_t parse_suboptions(
    struct string list, struct suboption *opts, size_t num_opts,
    bool is_runtime
);

/*
 * A head value followed by a number of suboptions to parse.
 * Example cmdline:
 *     - "my-param=foo,bar=123": "foo" is the head, "bar" is a suboption
 *     - "my-param=baz,x=1,y,z=2": "baz" is the head, "x", "y", and "z" are
 *                                 suboptions
 *
 *    In both cases the parameter setter receives the string after the '=',
 *    which it then parses via parse_suboptions_by_head() by giving it the
 *    list of acceptable suboption variants.
 */
struct suboption_variant {
    struct string head;
    struct suboption *opts;
    size_t num_opts;
};

/*
 * One of:
 * - SUBOPTION_VARIANT("foo")
 * - SUBOPTION_VARIANT("foo", foo_static_suboptions)
 * - SUBOPTION_VARIANT("foo", foo_runtime_suboptions, foo_num_suboptions)
 */
#define SUBOPTION_VARIANT(head, ...) \
    { STR(head), SUBOPTION_VARIANT_TABLE(__VA_ARGS__) }

/*
 * Parses a value of the form head[,sub-options] against the variants.
 * The index of the matching variant is returned through out_variant on
 * success.
 */
error_t parse_suboptions_by_head(
    struct string value, const struct suboption_variant *variants,
    size_t num_variants, size_t *out_variant, bool is_runtime
);
