#pragma once

#include <common/types.h>
#include <common/helpers.h>
#include <common/string_container.h>
#include <common/error.h>

#include <linker.h>

struct param {
    struct string name;
    const struct param_ops *ops;

    // TODO: permissions for sysfs
    // TODO: reference to the module defining this parameter

    void *value;
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

#define PARAM_TYPE_OPS(value) _Generic((value), \
    i8: g_param_i8_ops,                         \
    u8: g_param_u8_ops,                         \
    i16: g_param_i16_ops,                       \
    u16: g_param_u16_ops,                       \
    i32: g_param_i32_ops,                       \
    u32: g_param_u32_ops,                       \
    i64: g_param_i64_ops,                       \
    u64: g_param_u64_ops,                       \
    struct string: g_param_string_ops,          \
    bool: g_param_bool_ops                      \
)

#define PARAM_NAME(name)                                        \
    __builtin_choose_expr(                                      \
        __builtin_strncmp(#name, "g_", 2) == 0,                 \
        (struct string) { { &(#name)[2] }, sizeof(#name) - 3 }, \
        STR_CONSTEXPR(#name)                                    \
    )

#define custom_parameter_with_section(                         \
    name, value, ops, permissions, section                            \
)                                                                     \
    SECTION_VAR(section, static const, struct param) param_##name = { \
        PARAM_NAME(name), &(ops), &(value),                           \
    }

/*
 * Normal module parameters, these appear in sysfs and are only configured at
 * late init. Set 'perms' to 0 to hide from sysfs.
 */
#define custom_parameter(name, value, ops, perms)   \
    custom_parameter_with_section(                  \
        name, value, ops, perms, PARAMETERS_SECTION \
    )
#define parameter_with_ops(var, ops, perms) \
    custom_parameter(var, var, ops, perms)
#define parameter(var, perms) \
    parameter_with_ops(var, PARAM_TYPE_OPS(var), perms)

/*
 * Early parameters, these are parsed and set by the kernel as soon as possible
 * very early in the boot process.
 */
#define custom_early_parameter(name, value, ops)      \
    custom_parameter_with_section(                    \
        name, value, ops, 0, EARLY_PARAMETERS_SECTION \
    )
#define early_parameter_with_ops(var, ops) custom_early_parameter(var, var, ops)
#define early_parameter(var) early_parameter_with_ops(var, PARAM_TYPE_OPS(var))

typedef void (*unknown_param_cb_t)(struct string name, struct string arg);

// Parses the given command line and returns the string (if any) after --
struct string cmdline_parse(
    struct string cmdline, struct param *params, size_t num_params,
    unknown_param_cb_t unknown_cb
);
