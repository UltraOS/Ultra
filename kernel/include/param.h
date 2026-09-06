#pragma once

#include <common/types.h>
#include <common/helpers.h>
#include <common/string_container.h>
#include <common/error.h>

#include <linker.h>

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

struct param {
    struct string name;
    const struct param_ops *ops;

    // TODO: reference to the module defining this parameter

    void *value;
    u32 flags;

    // Size of the value in bytes for parameters stored in a char array
    size_t capacity;
};

struct param_ops {
    bool allows_empty_value;

    /*
     * Sets the value of the given parameter to one specified by the string.
     * Returns an error in case the operation wasn't successful.
     *
     * The string is only valid for the duration of the call. is_runtime is
     * false when the value comes from the kernel command line at boot and
     * true when it is being modified on a running system.
     */
    error_t (*set)(struct string, struct param*, bool is_runtime);

    /*
     * Converts a given parameter to a null-terminated string.
     *
     * The string size is the capacity of the buffer on entry and the number
     * of bytes written not including the terminating null on return. The
     * return value is the number of bytes required not including the
     * terminating null. If the required bytes don't fit along with the
     * terminating null, the buffer contents are unspecified.
     *
     * May be NULL for a parameter that cannot be read back, such a parameter
     * is never exposed to readers.
     */
    size_t (*get)(struct string*, struct param*);
};

// Helper for get callbacks that produce a string
size_t param_write_string(struct string *out, struct string value);

#define PARAMETER_OPS_DECL(type)                                  \
    error_t param_set_##type(struct string, struct param*, bool); \
    size_t param_get_##type(struct string*, struct param*);       \
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

#define PARAM_NAME(name)                                        \
    __builtin_choose_expr(                                      \
        __builtin_strncmp(#name, "g_", 2) == 0,                 \
        (struct string) { { &(#name)[2] }, sizeof(#name) - 3 }, \
        STR_CONSTEXPR(#name)                                    \
    )

#define custom_parameter_with_section(name, value, ops, flags, section)     \
    SECTION_VAR(section, static const, struct param) param_##name = {       \
        PARAM_NAME(name), &(ops), &(value), (flags), PARAM_CAPACITY(value), \
    }

/*
 * Parameters are set from the kernel command line and can be read back if
 * their ops provide a get callback. See enum param_flags for what a
 * parameter may additionally opt into.
 *
 * custom_parameter is the fully explicit form, the shorthands deduce the
 * name from the variable, the ops from its type, or both.
 */
#define custom_parameter(name, value, ops, flags)                              \
    custom_parameter_with_section(name, value, ops, flags, PARAMETERS_SECTION)

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
 * Early parameters, these are parsed and set by the kernel as soon as possible
 * very early in the boot process.
 */
#define custom_early_parameter(name, value, ops)      \
    custom_parameter_with_section(                    \
        name, value, ops, 0, EARLY_PARAMETERS_SECTION \
    )
#define early_parameter(var)                              \
    custom_early_parameter(var, var, PARAM_TYPE_OPS(var))

typedef void (*unknown_param_cb_t)(struct string name, struct string arg);

// Parses the given command line and returns the string (if any) after --
struct string cmdline_parse(
    struct string cmdline, struct param *params, size_t num_params,
    unknown_param_cb_t unknown_cb
);
