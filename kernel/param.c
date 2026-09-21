#define MSG_FMT(msg) "param: " msg

#include <common/conversions.h>
#include <common/format.h>
#include <common/ctype.h>

#include <param.h>

#define PARAM_SET_OPS_TEMPLATE(type)                              \
    error_t param_set_##type(                                     \
        struct string str, struct param_value *v, bool is_runtime \
    )                                                             \
    {                                                             \
        UNREFERENCED_PARAMETER(is_runtime);                       \
        return str_to_##type(str, v->ptr);                        \
    }                                                             \

#define PARAM_GET_OPS_TEMPLATE(type, fmt)                             \
    size_t param_get_##type(                                          \
        struct string *out_str, const struct param_value *v           \
    )                                                                 \
    {                                                                 \
        size_t bytes;                                                 \
                                                                      \
        bytes = (size_t)snprintf(                                     \
            out_str->mutable_text, out_str->size, fmt, *(type*)v->ptr \
        );                                                            \
        out_str->size = bytes < out_str->size ? bytes : 0;            \
                                                                      \
        return bytes;                                                 \
    }                                                                 \

#define PARAM_OPS(type)                             \
    const struct param_ops g_param_##type##_ops = { \
        .set = param_set_##type,                    \
        .get = param_get_##type,                    \
    }

#define MAKE_PARAM_OPS_WITH_FMT(type, fmt) \
    PARAM_SET_OPS_TEMPLATE(type)           \
    PARAM_GET_OPS_TEMPLATE(type, fmt)      \
    PARAM_OPS(type)

MAKE_PARAM_OPS_WITH_FMT(i8, "%d");
MAKE_PARAM_OPS_WITH_FMT(u8, "%u");
MAKE_PARAM_OPS_WITH_FMT(i16, "%d");
MAKE_PARAM_OPS_WITH_FMT(u16, "%u");
MAKE_PARAM_OPS_WITH_FMT(i32, "%d");
MAKE_PARAM_OPS_WITH_FMT(u32, "%u");
MAKE_PARAM_OPS_WITH_FMT(i64, "%lld");
MAKE_PARAM_OPS_WITH_FMT(u64, "%llu");

PARAM_GET_OPS_TEMPLATE(bool, "%d")

error_t param_set_bool(
    struct string str, struct param_value *v, bool is_runtime
)
{
    UNREFERENCED_PARAMETER(is_runtime);

    // Empty value means true, e.g. "bar" in "foo=1 bar baz=0"
    if (str_empty(str)) {
        *(bool*)v->ptr = true;
        return EOK;
    }

    return str_to_bool(str, v->ptr);
}

const struct param_ops g_param_bool_ops = {
    .allows_empty_value = true,
    .set = param_set_bool,
    .get = param_get_bool,
};

error_t param_set_string(
    struct string str, struct param_value *v, bool is_runtime
)
{
    UNREFERENCED_PARAMETER(is_runtime);

    if (str.size >= v->capacity)
        return ENOSPC;

    str_terminated_copy(v->ptr, str);
    return EOK;
}

size_t param_write_string(struct string *out, struct string value)
{
    if (value.size >= out->size) {
        out->size = 0;
        return value.size;
    }

    str_terminated_copy(out->mutable_text, value);
    out->size = value.size;
    return value.size;
}

size_t param_get_string(struct string *dst, const struct param_value *v)
{
    return param_write_string(dst, STR_RUNTIME((const char*)v->ptr));
}

PARAM_OPS(string);

/*
 * We want to be able to specify command line names as both "foo_bar" and
 * "foo-bar" to make it easier to get right.
 */
static char dash_to_underscore(char c)
{
    return c == '-' ? '_' : c;
}

static bool cmdline_name_compare(char lhs, char rhs)
{
    return dash_to_underscore(lhs) == dash_to_underscore(rhs);
}

static struct param *find_param(
    struct string name, struct param *params, size_t num_params
)
{
    size_t i;
    struct param *p;

    for (i = 0; i < num_params; i++) {
        p = &params[i];

        if (str_equals_with_cb(p->name, name, cmdline_name_compare))
            return p;
    }

    return nullptr;
}

static void cmdline_trim(struct string *cmdline)
{
    while (!str_empty(*cmdline) && isspace(cmdline->text[0]))
        str_offset_by(cmdline, 1);
}

static bool match_whitespace(struct string str)
{
    return isspace(str.text[0]);
}

static error_t param_set_checked(
    param_set_t set, bool allows_empty_value, struct param_value *v,
    struct string value, bool is_runtime
)
{
    if (unlikely(str_empty(value) && !allows_empty_value))
        return EINVAL;

    return set(value, v, is_runtime);
}

static struct suboption *find_suboption(
    struct string name, struct suboption *opts, size_t num_opts
)
{
    size_t i;

    for (i = 0; i < num_opts; i++) {
        if (str_equals_with_cb(opts[i].name, name, cmdline_name_compare))
            return &opts[i];
    }

    return nullptr;
}

/*
 * 'list' is the suboption we're parsing that came from 'whole'.
 * The latter is only used for nicer error messages.
 */
static error_t do_parse_suboptions(
    struct string list, char separator, struct suboption *opts,
    size_t num_opts, bool is_runtime, struct string whole
)
{
    struct string token, key, value;
    struct suboption *opt;
    ssize_t eq;
    error_t ret;
    u64 seen = 0;
    size_t i;

    BUG_ON(num_opts > BITS_PER_TYPE(seen));

    while (str_pop_token(&list, separator, &token)) {
        eq = str_find_one(token, '=', 0);
        if (eq < 0) {
            key = token;
            str_clear(&value);
        } else {
            key = str_substring(token, 0, eq);
            value = str_substring(token, eq + 1, token.size);
        }

        opt = find_suboption(key, opts, num_opts);
        if (opt == nullptr) {
            if (!is_runtime) {
                pr_err(
                    "unknown sub-option \"%pS\" in \"%pS\"\n", &key, &whole
                );
            }
            return EINVAL;
        }

        ret = param_set_checked(
            opt->set, opt->flags & SUBOPTION_ALLOWS_EMPTY_VALUE, &opt->value,
            value, is_runtime
        );
        if (is_error(ret)) {
            if (!is_runtime) {
                pr_err(
                    "bad sub-option \"%pS\" value \"%pS\" in \"%pS\" (%pE)\n",
                    &key, &value, &whole, &ret
                );
            }
            return ret;
        }

        seen |= BIT_U64(opt - opts);
    }

    for (i = 0; i < num_opts; i++) {
        if (!(opts[i].flags & SUBOPTION_REQUIRED) || (seen & BIT_U64(i)))
            continue;

        if (!is_runtime)
            pr_err(
                "missing sub-option \"%pS\" in \"%pS\"\n", &opts[i].name,
                &whole
            );
        return EINVAL;
    }

    return EOK;
}

error_t parse_suboptions_with_separator(
    struct string list, char separator, struct suboption *opts,
    size_t num_opts, bool is_runtime
)
{
    return do_parse_suboptions(
        list, separator, opts, num_opts, is_runtime, list
    );
}

error_t parse_suboptions(
    struct string list, struct suboption *opts, size_t num_opts,
    bool is_runtime
)
{
    return parse_suboptions_with_separator(
        list, ',', opts, num_opts, is_runtime
    );
}

error_t parse_suboptions_by_head(
    struct string value, const struct suboption_variant *variants,
    size_t num_variants, size_t *out_variant, bool is_runtime
)
{
    struct string rest = value, head;
    error_t ret;
    size_t i;

    if (!str_pop_token(&rest, ',', &head) || str_empty(head)) {
        if (!is_runtime)
            pr_err("missing head in \"%pS\"\n", &value);
        return EINVAL;
    }

    for (i = 0; i < num_variants; i++) {
        if (str_equals_with_cb(variants[i].head, head, cmdline_name_compare))
            break;
    }
    if (i == num_variants) {
        if (!is_runtime)
            pr_err("unknown \"%pS\" in \"%pS\"\n", &head, &value);
        return EINVAL;
    }

    ret = do_parse_suboptions(
        rest, ',', variants[i].opts, variants[i].num_opts, is_runtime, value
    );
    if (is_error(ret))
        return ret;

    *out_variant = i;
    return EOK;
}

static struct param *find_param_in_tables(
    struct string name, const struct param_table *tables, size_t num_tables
)
{
    struct param *p;
    size_t i;

    for (i = 0; i < num_tables; i++) {
        p = find_param(name, tables[i].params, tables[i].count);
        if (p != nullptr)
            return p;
    }

    return nullptr;
}

static void warn_unknown_param(struct string name, struct string value)
{
    if (str_empty(value)) {
        pr_warn("unknown parameter \"%pS\"\n", &name);
        return;
    }

    pr_warn("unknown parameter \"%pS\" (value \"%pS\")\n", &name, &value);
}

struct string cmdline_parse_tables(
    struct string cmdline, const struct param_table *tables, size_t num_tables
)
{
    error_t ret;
    struct string key, value;
    struct param *p;

    cmdline_trim(&cmdline);

    for (;;) {
        key = (struct string) {
            .text = cmdline.text,
            .size = 0,
        };
        str_clear(&value);

        while (!str_empty(cmdline)) {
            // We're done, the rest of the arguments are for init
            if (str_starts_with(cmdline, STR("--"))) {
                str_offset_by(&cmdline, 2);
                cmdline_trim(&cmdline);
                return cmdline;
            }

            // Found a space, this is the key, value is empty, we're done
            if (isspace(cmdline.text[0])) {
                cmdline_trim(&cmdline);
                break;
            }

            // This key has a value as well, parse it
            if (cmdline.text[0] == '=') {
                ssize_t value_end;

                str_offset_by(&cmdline, 1);

                if (!str_empty(cmdline) && cmdline.text[0] == '"') {
                    str_offset_by(&cmdline, 1);
                    value_end = str_find_one(cmdline, '\"', 0);
                } else {
                    value_end = str_find_with_cb(cmdline, match_whitespace, 0);
                }

                if (value_end == -1)
                    value_end = cmdline.size;

                value = str_substring(cmdline, 0, value_end);
                str_offset_by(&cmdline, value_end);
                break;
            }

            str_extend_by(&key, 1);
            str_offset_by(&cmdline, 1);
        }

        if (str_empty(key))
            return cmdline;

        p = find_param_in_tables(key, tables, num_tables);
        if (p == nullptr) {
            warn_unknown_param(key, value);
            goto do_next;
        }

        ret = param_set_checked(
            p->ops->set, p->ops->allows_empty_value, &p->value, value, false
        );
        if (is_error(ret)) {
            pr_err(
                "bad \"%pS\" value \"%pS\" (%pE)\n", &key, &value, &ret
            );
        }

    do_next:
        cmdline_trim(&cmdline);
    }
}

struct string cmdline_parse(
    struct string cmdline, struct param *params, size_t num_params
)
{
    struct param_table table = { params, num_params };

    return cmdline_parse_tables(cmdline, &table, 1);
}
