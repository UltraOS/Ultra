#include <kernel-source/param.c>
#include <test_harness.h>

struct param g_params[16];
struct string g_cmdline;

#define TYPES        \
    TYPE(bool, bool) \
    TYPE(i8, i8)     \
    TYPE(u8, u8)     \
    TYPE(i16, i16)   \
    TYPE(u16, u16)   \
    TYPE(i32, i32)   \
    TYPE(u32, u32)   \
    TYPE(i64, i64)   \
    TYPE(u64, u64)

enum stored_type {
#define TYPE(name, type) st_##name,
        TYPES
#undef TYPE
    st_string,
};

struct test_param {
    union test_value {
    #define TYPE(name, type) type as_##name;
        TYPES
    #undef TYPE
        char as_string[16];
    } value, expect;

    enum stored_type stored_type;

    struct param param;
};

#define DEFINE_PARAM(type, param_name, init, expected) \
    {                                                  \
        .value = { .as_##type = init, },               \
        .expect = { .as_##type = expected, },          \
        .stored_type = st_##type,                      \
        .param = {                                     \
            .name = STR(#param_name),                  \
            .ops = &g_param_##type##_ops,              \
        },                                             \
    }

#define CMDLINE_TEST(cmdline, ...)                        \
    struct test_param params[] = {                        \
        __VA_ARGS__                                       \
    };                                                    \
    BUILD_BUG_ON_WITH_MSG(                                \
        ARRAY_SIZE(params) > ARRAY_SIZE(g_params),        \
        "Increase the size of the static parameter array" \
    );                                                    \
                                                          \
    for (size_t i = 0; i < ARRAY_SIZE(params); i++) {     \
        params[i].param.value.ptr = &params[i].value;     \
        if (params[i].stored_type == st_string) {         \
            params[i].param.value.capacity =              \
                sizeof(params[i].value.as_string);        \
        }                                                 \
        g_params[i] = params[i].param;                    \
    }                                                     \
    g_cmdline = STR(cmdline)

#define CMDLINE_PARSE()                                    \
    cmdline_parse(g_cmdline, g_params, ARRAY_SIZE(params))

#define CMDLINE_PARSE_EXPECT(str)                   \
    ASSERT(str_equals(CMDLINE_PARSE(), STR(str)));  \
    for (size_t i = 0; i < ARRAY_SIZE(params); i++) \
        ensure_param_matches(&params[i])

#define CMDLINE_PARSE_CHECKED() CMDLINE_PARSE_EXPECT("")

static void ensure_param_matches(struct test_param *param)
{
    switch (param->stored_type) {
    #define TYPE(name, type)                                            \
        case st_##name:                                                 \
            ASSERT_EQ(param->value.as_##type, param->expect.as_##type); \
            break;
        TYPES
    #undef TYPE
    case st_string:
        ASSERT(str_equals(
            STR_RUNTIME(param->value.as_string),
            STR_RUNTIME(param->expect.as_string)
        ));
        break;
    default:
        ASSERT(false);
    }
}

TEST_CASE(parse_singular)
{
    CMDLINE_TEST(
        "foo baz",
        DEFINE_PARAM(bool, foo, false, true),
        DEFINE_PARAM(bool, bar, true, true),
        DEFINE_PARAM(bool, baz, false, true),
    );

    CMDLINE_PARSE_CHECKED();
}

TEST_CASE(parse_spaces)
{
    CMDLINE_TEST(
        "      foo=1          bar\t\t\t\t\t\vbaz=off\t\t\t\t\v       x\t\t\t",
        DEFINE_PARAM(u8, foo, 0, 1),
        DEFINE_PARAM(bool, bar, false, true),
        DEFINE_PARAM(bool, baz, true, false),
        DEFINE_PARAM(bool, x, false, true),
    );

    CMDLINE_PARSE_CHECKED();
}

TEST_CASE(parse_empty_assignment)
{
    /*
     * This behavior is different from e.g. the linux command line parser, where
     * a lack of value is different from the value just being "\0" (this is
     * considered an error in linux). Don't see any reason to copy that
     * inconsistency.
     */
    CMDLINE_TEST(
        "x= y z=",
        DEFINE_PARAM(bool, x, false, true),
        DEFINE_PARAM(bool, y, false, true),
        DEFINE_PARAM(bool, z, false, true),
    );

    CMDLINE_PARSE_CHECKED();
}

TEST_CASE(parse_mixed)
{
    CMDLINE_TEST(
        "foo bar=123 baz=on beef=dead cafe=\"123 321\" x=-3 y=0xDEADBEF",
        DEFINE_PARAM(bool, foo, false, true),
        DEFINE_PARAM(u32, bar, 0, 123),
        DEFINE_PARAM(bool, baz, false, true),
        DEFINE_PARAM(string, beef, "alive", "dead"),
        DEFINE_PARAM(string, cafe, "", "123 321"),
        DEFINE_PARAM(i8, x, 0, -3),
        DEFINE_PARAM(u64, y, 0, 0xDEADBEF),
    );

    CMDLINE_PARSE_CHECKED();
}

TEST_CASE(parse_end)
{
    CMDLINE_TEST(
        "foo bar hello=\"  ==world==\"     ----x=y--test 123",
        DEFINE_PARAM(string, hello, "", "  ==world=="),
    );

    CMDLINE_PARSE_EXPECT("--x=y--test 123");
}

TEST_CASE(bad_values)
{
    CMDLINE_TEST(
        "x=128 y=256 z=0xFFFF a=0xFFFF b=2147483647 c=\"-2147483648\" d=2147483648",
        DEFINE_PARAM(i8, x, 3, 3),
        DEFINE_PARAM(u8, y, 32, 32),
        DEFINE_PARAM(u16, z, 235, 0xFFFF),
        DEFINE_PARAM(i16, a, 321, 321),
        DEFINE_PARAM(i32, b, 123, 2147483647),
        DEFINE_PARAM(i32, c, 123, -2147483648),
        DEFINE_PARAM(i32, d, 123, 123),
    );

    CMDLINE_PARSE_CHECKED();
}

TEST_CASE(string_capacity)
{
    CMDLINE_TEST(
        "fits=0123456789abcde full=0123456789abcdef over=\"0123456789abcdefg\"",
        DEFINE_PARAM(string, fits, "x", "0123456789abcde"),
        DEFINE_PARAM(string, full, "x", "x"),
        DEFINE_PARAM(string, over, "x", "x"),
    );

    CMDLINE_PARSE_CHECKED();
}

TEST_CASE(dashes_are_underscores)
{
    CMDLINE_TEST(
        "foo_bar=123 cafe-babe=321",
        DEFINE_PARAM(u64, foo_bar, 0, 123),
        DEFINE_PARAM(i64, cafe_babe, 0, 321),
    );

    CMDLINE_PARSE_CHECKED();
}

TEST_CASE(bools)
{
    CMDLINE_TEST(
        "x=0 y=1 z=t a=f b=\"0\" c=1 d=on e=off f=\"Y\" \"g=Y\" g=N h=ON i=Off -- e=1 a=T",
        DEFINE_PARAM(bool, x, true, false),
        DEFINE_PARAM(bool, y, false, true),
        DEFINE_PARAM(bool, z, false, true),
        DEFINE_PARAM(bool, a, true, false),
        DEFINE_PARAM(bool, b, true, false),
        DEFINE_PARAM(bool, c, false, true),
        DEFINE_PARAM(bool, d, false, true),
        DEFINE_PARAM(bool, e, true, false),
        DEFINE_PARAM(bool, f, false, true),
        DEFINE_PARAM(bool, g, true, false),
        DEFINE_PARAM(bool, h, false, true),
        DEFINE_PARAM(bool, i, true, false),
    );

    CMDLINE_PARSE_EXPECT("e=1 a=T");
}

TEST_CASE(get_fits)
{
    char buf[8];
    struct string out = MAKE_STR(buf, sizeof(buf));
    u32 value = 1234567;
    struct param p = {
        .name = STR("x"),
        .ops = &g_param_u32_ops,
        .value.ptr = &value,
    };

    ASSERT_EQ(param_get_u32(&out, &p.value), 7);
    ASSERT_EQ(out.size, 7);
    ASSERT(str_equals(out, STR("1234567")));
    ASSERT_EQ(buf[7], '\0');
}

TEST_CASE(get_overflow)
{
    char buf[8];
    struct string out = MAKE_STR(buf, sizeof(buf));
    u32 value = 12345678;
    char text[] = "12345678";
    struct param p = {
        .name = STR("x"),
        .ops = &g_param_u32_ops,
        .value.ptr = &value,
    };

    ASSERT_EQ(param_get_u32(&out, &p.value), 8);
    ASSERT_EQ(out.size, 0);

    out = MAKE_STR(buf, sizeof(buf));
    p.ops = &g_param_string_ops;
    p.value.ptr = text;
    p.value.capacity = sizeof(text);

    ASSERT_EQ(param_get_string(&out, &p.value), 8);
    ASSERT_EQ(out.size, 0);

    out = MAKE_STR(buf, 0);
    ASSERT_EQ(param_get_string(&out, &p.value), 8);
    ASSERT_EQ(out.size, 0);
}

static struct string s_action_values[4];
static size_t s_action_calls;

static error_t action_set(
    struct string value, struct param_value *v, bool is_runtime
)
{
    if (v->ptr != nullptr || is_runtime)
        return EINVAL;

    s_action_values[s_action_calls++] = value;
    return EOK;
}

TEST_CASE(action)
{
    const struct param_ops ops = {
        .allows_empty_value = true,
        .set = action_set,
    };
    struct param p = {
        .name = STR("act"),
        .ops = &ops,
    };

    s_action_calls = 0;
    cmdline_parse(STR("act act=1 act= act=\"a b\""), &p, 1);

    ASSERT_EQ(s_action_calls, 4);
    ASSERT(str_equals(s_action_values[0], STR("")));
    ASSERT(str_equals(s_action_values[1], STR("1")));
    ASSERT(str_equals(s_action_values[2], STR("")));
    ASSERT(str_equals(s_action_values[3], STR("a b")));
}

TEST_CASE(suboptions_flags_and_values)
{
    bool colored = false;
    u32 baud = 0;
    char name[8] = "";
    struct suboption opts[] = {
        suboption(colored), suboption(baud), suboption(name),
    };

    ASSERT_EQ(
        parse_suboptions(
            STR("colored,baud=115200,name=ttyS0"), opts,
            ARRAY_SIZE(opts), false
        ),
        EOK
    );
    ASSERT_TRUE(colored);
    ASSERT_EQ(baud, 115200);
    ASSERT_STR_EQ(name, "ttyS0");
}

TEST_CASE(suboptions_null_is_empty)
{
    bool colored = false;
    struct suboption opts[] = { suboption(colored) };

    ASSERT_EQ(
        parse_suboptions(
            NULL_STR(), opts,
            ARRAY_SIZE(opts), false
        ),
        EOK
    );
    ASSERT_FALSE(colored);
}

TEST_CASE(suboptions_rejects_empty_entries)
{
    bool colored = false;
    struct suboption opts[] = { suboption(colored) };

    ASSERT_EQ(
        parse_suboptions(
            STR(""), opts,
            ARRAY_SIZE(opts), false
        ),
        EINVAL
    );
    ASSERT_EQ(
        parse_suboptions(
            STR("colored,"), opts,
            ARRAY_SIZE(opts), false
        ),
        EINVAL
    );
    ASSERT_EQ(
        parse_suboptions(
            STR(",colored"), opts,
            ARRAY_SIZE(opts), false
        ),
        EINVAL
    );
}

TEST_CASE(suboptions_rejects_unknown_keys_and_bad_values)
{
    bool colored = false;
    u32 baud = 0;
    struct suboption opts[] = { suboption(colored), suboption(baud) };

    ASSERT_EQ(
        parse_suboptions(
            STR("bogus"), opts,
            ARRAY_SIZE(opts), false
        ),
        EINVAL
    );
    ASSERT_EQ(
        parse_suboptions(
            STR("baud=fast"), opts,
            ARRAY_SIZE(opts), false
        ),
        EINVAL
    );
    ASSERT_EQ(
        parse_suboptions(
            STR("baud"), opts,
            ARRAY_SIZE(opts), false
        ),
        EINVAL
    );
}

TEST_CASE(suboptions_stops_at_the_first_error)
{
    bool a = false, b = false;
    struct suboption opts[] = { suboption(a), suboption(b) };

    ASSERT_EQ(
        parse_suboptions(
            STR("a,bogus,b"), opts,
            ARRAY_SIZE(opts), false
        ),
        EINVAL
    );
    ASSERT_TRUE(a);
    ASSERT_FALSE(b);
}

TEST_CASE(suboptions_names)
{
    bool s_no_color = false, bright = false;
    struct suboption opts[] = {
        suboption(s_no_color), renamed_suboption(vivid, bright),
    };

    ASSERT_EQ(
        parse_suboptions(
            STR("no-color,vivid=on"), opts, ARRAY_SIZE(opts), false
        ),
        EOK
    );
    ASSERT_TRUE(s_no_color);
    ASSERT_TRUE(bright);
}

static size_t g_action_calls;
static bool g_action_runtime;

static error_t count_action(
    struct string value, struct param_value *v, bool rt
)
{
    UNREFERENCED_PARAMETER(v);

    g_action_calls += str_empty(value) ? 1 : 10;
    g_action_runtime = rt;
    return EOK;
}

TEST_CASE(suboptions_actions)
{
    struct suboption opts[] = { action_suboption(tick, count_action) };

    g_action_calls = 0;
    ASSERT_EQ(
        parse_suboptions_with_separator(
            STR("tick,tick=x"), ';', opts, ARRAY_SIZE(opts), true
        ),
        EINVAL
    );
    ASSERT_EQ(g_action_calls, 0);

    ASSERT_EQ(
        parse_suboptions_with_separator(
            STR("tick;tick=x"), ';', opts, ARRAY_SIZE(opts), true
        ),
        EOK
    );
    ASSERT_EQ(g_action_calls, 11);
    ASSERT_TRUE(g_action_runtime);
}

TEST_CASE(tables_are_searched_in_order)
{
    bool a = false, b = false;
    struct param first[] = { PARAM_ENTRY(a, a, g_param_bool_ops, 0) };
    struct param second[] = { PARAM_ENTRY(b, b, g_param_bool_ops, 0) };
    struct param_table tables[] = {
        { first, ARRAY_SIZE(first) }, { second, ARRAY_SIZE(second) },
    };

    ASSERT(str_equals(
        cmdline_parse_tables(STR("b a -- init"), tables, ARRAY_SIZE(tables)),
        STR("init")
    ));
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
}

TEST_CASE(by_head_selects_the_variant)
{
    bool colored = false;
    u32 port = 0;
    size_t variant = 99;
    struct suboption e9_opts[] = { suboption(colored) };
    struct suboption serial_opts[] = { suboption(port), suboption(colored) };
    struct suboption_variant variants[] = {
        SUBOPTION_VARIANT("none"),
        SUBOPTION_VARIANT("e9", e9_opts),
        SUBOPTION_VARIANT("serial", serial_opts),
    };

    ASSERT_EQ(
        parse_suboptions_by_head(
            STR("serial,port=3,colored"), variants, ARRAY_SIZE(variants),
            &variant, false
        ),
        EOK
    );
    ASSERT_EQ(variant, 2);
    ASSERT_EQ(port, 3);
    ASSERT_TRUE(colored);

    ASSERT_EQ(
        parse_suboptions_by_head(
            STR("e9"), variants, ARRAY_SIZE(variants), &variant, false
        ),
        EOK
    );
    ASSERT_EQ(variant, 1);
}

TEST_CASE(by_head_rejects_unknown_heads_and_options)
{
    bool colored = false;
    size_t variant = 99;
    struct suboption e9_opts[] = { suboption(colored) };
    struct suboption_variant variants[] = {
        SUBOPTION_VARIANT("none"),
        SUBOPTION_VARIANT("e9", e9_opts),
    };

    ASSERT_EQ(
        parse_suboptions_by_head(
            STR("bogus,colored"), variants, ARRAY_SIZE(variants), &variant,
            false
        ),
        EINVAL
    );
    ASSERT_EQ(
        parse_suboptions_by_head(
            STR("none,colored"), variants, ARRAY_SIZE(variants), &variant,
            false
        ),
        EINVAL
    );
    ASSERT_EQ(
        parse_suboptions_by_head(
            STR(""), variants, ARRAY_SIZE(variants), &variant, false
        ),
        EINVAL
    );
    ASSERT_EQ(
        parse_suboptions_by_head(
            STR(",colored"), variants, ARRAY_SIZE(variants), &variant, false
        ),
        EINVAL
    );
    ASSERT_EQ(variant, 99);
    ASSERT_FALSE(colored);
}

TEST_CASE(by_head_takes_a_pointer_and_count)
{
    bool colored = false;
    size_t variant = 99;
    struct suboption table[] = { suboption(colored) };
    struct suboption *ptr = table;
    struct suboption_variant variants[] = {
        SUBOPTION_VARIANT("none"),
        SUBOPTION_VARIANT("e9", ptr, ARRAY_SIZE(table)),
    };

    ASSERT_EQ(
        parse_suboptions_by_head(
            STR("e9,colored"), variants, ARRAY_SIZE(variants), &variant, false
        ),
        EOK
    );
    ASSERT_EQ(variant, 1);
    ASSERT_TRUE(colored);
    ASSERT_EQ(variants[0].num_opts, 0);
    ASSERT_NULL(variants[0].opts);
}
