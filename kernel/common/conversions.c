#include <common/conversions.h>
#include <common/ctype.h>

static unsigned int consume_base(struct string *str)
{
    if (unlikely(str_empty(*str)))
        return 0;

    if (str_starts_with(*str, STR("0x"))) {
        str_offset_by(str, 2);
        return 16;
    }

    if (str_starts_with(*str, STR("0b"))) {
        str_offset_by(str, 2);
        return 2;
    }

    if (str_starts_with(*str, STR("0"))) {
        str_offset_by(str, 1);
        return 8;
    }

    if (isdigit(str->text[0]))
        return 10;

    return 0;
}

static error_t do_str_to_u64_unchecked(
    struct string str, u64 *res, unsigned int base
)
{
    u64 number = 0;
    u64 next;
    char c;

    while (str_pop_one(&str, &c)) {
        if (isdigit(c)) {
            next = c - '0';
        } else {
            char l = tolower(c);
            if (!isxdigit(c))
                return EINVAL;
            next = 10 + l - 'a';
        }

        next = number * base + next;
        if (next / base != number)
            return ERANGE;
        number = next;
    }

    *res = number;
    return EOK;
}

static error_t do_str_to_u64(struct string str, u64 *res, unsigned int base)
{
    unsigned int cb = consume_base(&str);
    if (!base && !cb)
        return EINVAL;

    return do_str_to_u64_unchecked(str, res, base ?: cb);
}

error_t str_to_i64_with_base(struct string str, i64 *res, unsigned int base)
{
    u64 ures;
    error_t ret;

    if (str_starts_with(str, STR("-"))) {
        str_offset_by(&str, 1);

        ret = do_str_to_u64(str, &ures, base);
        if (is_error(ret))
            return ret;
        if ((i64)-ures > 0)
            return ERANGE;
    } else {
        if (str_starts_with(str, STR("+")))
            str_offset_by(&str, 1);

        ret = do_str_to_u64(str, &ures, base);
        if (is_error(ret))
            return ret;
        if (ures > (u64)INT64_MAX)
            return ERANGE;
    }

    *res = (i64)ures;
    return EOK;
}

error_t str_to_u64_with_base(struct string str, u64 *res, unsigned int base)
{
    if (str_starts_with(str, STR("+")))
        str_offset_by(&str, 1);

    if (str_starts_with(str, STR("-")))
        return EINVAL;

    return do_str_to_u64(str, res, base);
}

error_t str_to_i32_with_base(struct string str, i32 *res, unsigned int base)
{
    i64 ires;
    error_t ret;

    ret = str_to_i64_with_base(str, &ires, base);
    if (is_error(ret))
        return ret;

    if ((i32)ires != ires)
        return ERANGE;

    *res = (i32)ires;
    return EOK;
}

error_t str_to_u32_with_base(struct string str, u32 *res, unsigned int base)
{
    u64 ures;
    error_t ret;

    ret = str_to_u64_with_base(str, &ures, base);
    if (is_error(ret))
        return ret;

    if ((u32)ures != ures)
        return ERANGE;

    *res = (u32)ures;
    return EOK;
}

error_t str_to_i16_with_base(struct string str, i16 *res, unsigned int base)
{
    i64 ires;
    error_t ret;

    ret = str_to_i64_with_base(str, &ires, base);
    if (is_error(ret))
        return ret;

    if ((i16)ires != ires)
        return ERANGE;

    *res = (i16)ires;
    return EOK;
}

error_t str_to_u16_with_base(struct string str, u16 *res, unsigned int base)
{
    u64 ures;
    error_t ret;

    ret = str_to_u64_with_base(str, &ures, base);
    if (is_error(ret))
        return ret;

    if ((u16)ures != ures)
        return ERANGE;

    *res = (u16)ures;
    return EOK;
}

error_t str_to_i8_with_base(struct string str, i8 *res, unsigned int base)
{
    i64 ires;
    error_t ret;

    ret = str_to_i64_with_base(str, &ires, base);
    if (is_error(ret))
        return ret;

    if ((i8)ires != ires)
        return ERANGE;

    *res = (i8)ires;
    return EOK;
}

error_t str_to_u8_with_base(struct string str, u8 *res, unsigned int base)
{
    u64 ures;
    error_t ret;

    ret = str_to_u64_with_base(str, &ures, base);
    if (is_error(ret))
        return ret;

    if ((u8)ures != ures)
        return ERANGE;

    *res = (u8)ures;
    return EOK;
}

error_t str_to_bool(struct string str, bool *res)
{
    size_t i;

    static const struct string options[] = {
        // True options
        STR_CONSTEXPR("y"),
        STR_CONSTEXPR("t"),
        STR_CONSTEXPR("on"),
        STR_CONSTEXPR("1"),
        #define NUM_TRUE_OPTIONS 4

        // False options
        STR_CONSTEXPR("n"),
        STR_CONSTEXPR("f"),
        STR_CONSTEXPR("off"),
        STR_CONSTEXPR("0"),
    };

    for (i = 0; i < ARRAY_SIZE(options); i++) {
        if (!str_equals_caseless(str, options[i]))
            continue;

        *res = i < NUM_TRUE_OPTIONS;
        return EOK;
    }

    return EINVAL;
}
