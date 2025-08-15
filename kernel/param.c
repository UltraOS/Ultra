#define MSG_FMT(msg) "param: " msg

#include <common/conversions.h>
#include <common/format.h>
#include <common/minmax.h>
#include <common/ctype.h>

#include <param.h>

#define PARAM_SET_OPS_TEMPLATE(type)                                    \
    error_t param_set_##type(struct string str, struct param *p)        \
    {                                                                   \
        return str_to_##type(str, p->value);                            \
    }                                                                   \

#define PARAM_GET_OPS_TEMPLATE(type, fmt)                               \
    size_t param_get_##type(                                            \
        struct string *out_str, struct param *p                         \
    )                                                                   \
    {                                                                   \
        size_t bytes;                                                   \
                                                                        \
        bytes = (size_t)snprintf(                                       \
            out_str->mutable_text, out_str->size, fmt, *(type*)p->value \
        );                                                              \
        out_str->size = MIN(bytes, out_str->size);                      \
                                                                        \
        return bytes;                                                   \
    }                                                                   \

#define PARAM_OPS(type)                                                 \
    const struct param_ops g_param_##type##_ops = {                     \
        .set = param_set_##type,                                        \
        .get = param_get_##type,                                        \
    }

#define MAKE_PARAM_OPS_WITH_FMT(type, fmt)                              \
    PARAM_SET_OPS_TEMPLATE(type)                                        \
    PARAM_GET_OPS_TEMPLATE(type, fmt)                                   \
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

error_t param_set_bool(struct string str, struct param *p)
{
    // Empty value means true, e.g. "bar" in "foo=1 bar baz=0"
    if (str_empty(str)) {
        *(bool*)p->value = true;
        return EOK;
    }

    return str_to_bool(str, p->value);
}

const struct param_ops g_param_bool_ops = {
    .allows_empty_value = true,
    .set = param_set_bool,
    .get = param_get_bool,
};

error_t param_set_string(struct string str, struct param *p)
{
    *(struct string*)p->value = str;
    return EOK;
}

size_t param_get_string(struct string *dst, struct param *p)
{
    struct string *src = p->value;

    memcpy(dst->mutable_text, src->text, MIN(src->size, dst->size));
    if (dst->size > src->size)
        dst->mutable_text[src->size] = '\0';

    dst->size = MIN(src->size, dst->size);
    return src->size;
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

    return NULL;
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

struct string cmdline_parse(
    struct string cmdline, struct param *params, size_t num_params,
    unknown_param_cb_t unknown_cb
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

        p = find_param(key, params, num_params);
        if (p == NULL) {
            if (unknown_cb != NULL)
                unknown_cb(key, value);
            goto do_next;
        }

        if (likely(!str_empty(value) || p->ops->allows_empty_value))
            ret = p->ops->set(value, p);
        else
            ret = EINVAL;

        if (is_error(ret)) {
            pr_err(
                "bad \"%pS\" value \"%pS\" (%d)\n", &key, &value, ret
            );
        }

    do_next:
        cmdline_trim(&cmdline);
    }
}
