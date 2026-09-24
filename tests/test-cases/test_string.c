#include <common/string_container.h>

#include <test_harness.h>

#define ASSERT_TOKEN(str, expected)                      \
    do {                                                 \
        struct string token;                             \
        ASSERT_TRUE(str_pop_token(&(str), ',', &token)); \
        ASSERT_TRUE(str_equals(token, STR(expected)));   \
    } while (0)

#define ASSERT_EXHAUSTED(str)                             \
    do {                                                  \
        struct string token;                              \
        ASSERT_FALSE(str_pop_token(&(str), ',', &token)); \
        ASSERT_NULL((str).text);                          \
    } while (0)

TEST_CASE(pop_token_list)
{
    struct string str = STR("a,bb,ccc");

    ASSERT_TOKEN(str, "a");
    ASSERT_TOKEN(str, "bb");
    ASSERT_TOKEN(str, "ccc");
    ASSERT_EXHAUSTED(str);
}

TEST_CASE(pop_token_single)
{
    struct string str = STR("alone");

    ASSERT_TOKEN(str, "alone");
    ASSERT_EXHAUSTED(str);
}

TEST_CASE(pop_token_empty_tokens_are_reported)
{
    struct string str = STR("a,,b");

    ASSERT_TOKEN(str, "a");
    ASSERT_TOKEN(str, "");
    ASSERT_TOKEN(str, "b");
    ASSERT_EXHAUSTED(str);
}

TEST_CASE(pop_token_trailing_separator)
{
    struct string str = STR("a,");

    ASSERT_TOKEN(str, "a");
    ASSERT_TOKEN(str, "");
    ASSERT_EXHAUSTED(str);
}

TEST_CASE(pop_token_leading_separator)
{
    struct string str = STR(",a");

    ASSERT_TOKEN(str, "");
    ASSERT_TOKEN(str, "a");
    ASSERT_EXHAUSTED(str);
}

TEST_CASE(pop_token_empty_string_is_one_empty_token)
{
    struct string str = STR("");

    ASSERT_TOKEN(str, "");
    ASSERT_EXHAUSTED(str);
}

TEST_CASE(pop_token_null_string_has_no_tokens)
{
    struct string str = NULL_STR();

    ASSERT_EXHAUSTED(str);
}

TEST_CASE(starts_with_caseless)
{
    ASSERT_TRUE(str_starts_with_caseless(STR("Asus X"), STR("ASUS")));
    ASSERT_TRUE(str_starts_with_caseless(STR("asus"), STR("ASUS")));
    ASSERT_TRUE(str_starts_with_caseless(STR("asus"), STR("")));
    ASSERT_FALSE(str_starts_with_caseless(STR("ASU"), STR("ASUS")));
    ASSERT_FALSE(str_starts_with_caseless(STR("BSUS X"), STR("ASUS")));
}
