#include <kernel-source/param.c>
#include <kernel-source/common/string_container.c>
#include <kernel-source/common/conversions.c>
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
    union param_value {
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
        params[i].param.value = &params[i].value;         \
        if (params[i].stored_type == st_string) {         \
            params[i].param.capacity =                    \
                sizeof(params[i].value.as_string);        \
        }                                                 \
        g_params[i] = params[i].param;                    \
    }                                                     \
    g_cmdline = STR(cmdline)

#define CMDLINE_PARSE() \
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
        .value = &value,
    };

    ASSERT_EQ(param_get_u32(&out, &p), 7);
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
        .value = &value,
    };

    ASSERT_EQ(param_get_u32(&out, &p), 8);
    ASSERT_EQ(out.size, 0);

    out = MAKE_STR(buf, sizeof(buf));
    p.ops = &g_param_string_ops;
    p.value = text;
    p.capacity = sizeof(text);

    ASSERT_EQ(param_get_string(&out, &p), 8);
    ASSERT_EQ(out.size, 0);

    out = MAKE_STR(buf, 0);
    ASSERT_EQ(param_get_string(&out, &p), 8);
    ASSERT_EQ(out.size, 0);
}
