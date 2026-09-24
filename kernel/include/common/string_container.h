#pragma once

#include <common/types.h>
#include <common/string.h>
#include <common/ctype.h>

#include <bug.h>

struct string {
    union {
        const char *text;
        char *mutable_text;
    };
    size_t size;
};

#define MAKE_STR(ptr, size) (struct string) { { (ptr) }, (size) }
#define NULL_STR() MAKE_STR(nullptr, 0)
#define STR_CONSTEXPR(str) MAKE_STR(str, sizeof((str)) - 1)
#define STR_RUNTIME(str) MAKE_STR(str, (str) ? strlen((str)) : 0)

#define STR(str) \
    CHOOSE_EXPR(IS_CONSTEXPR((str)), STR_CONSTEXPR((str)), STR_RUNTIME(str))

bool str_equals(struct string lhs, struct string rhs);
bool str_equals_with_cb(
    struct string lhs, struct string rhs,
    bool (*are_equal)(char, char)
);

static inline bool chars_caseless_compare(char lhs, char rhs)
{
    return tolower(lhs) == tolower(rhs);
}

static inline bool str_equals_caseless(struct string lhs, struct string rhs)
{
    return str_equals_with_cb(lhs, rhs, chars_caseless_compare);
}

bool str_starts_with(struct string str, struct string prefix);
bool str_starts_with_with_cb(
    struct string str, struct string prefix,
    bool (*are_equal)(char, char)
);

static inline bool str_starts_with_caseless(
    struct string str, struct string prefix
)
{
    return str_starts_with_with_cb(str, prefix, chars_caseless_compare);
}

ssize_t str_find_with_cb(
    struct string str, bool (*is_match)(struct string str), size_t starting_at
);
ssize_t str_find(struct string str, struct string needle, size_t starting_at);

static inline ssize_t str_find_one(
    struct string str, char needle, size_t starting_at
)
{
    size_t i;

    for (i = starting_at; i < str.size; ++i) {
        if (str.text[i] != needle)
            continue;

        return i;
    }

    return -1;
}

static inline struct string str_substring(
    struct string str, size_t start_idx, size_t end_idx
)
{
    if (unlikely(start_idx > end_idx))
        return NULL_STR();

    BUG_ON(end_idx > str.size);

    return (struct string) {
        .text = str.text + start_idx,
        .size = end_idx - start_idx,
    };
}

static inline bool str_empty(struct string str)
{
    return str.size == 0;
}

static inline bool str_is_null(struct string str)
{
    return str.text == nullptr;
}

static inline bool str_contains(struct string str, struct string needle)
{
    return str_find(str, needle, 0) >= 0;
}

static inline void str_offset_by(struct string *str, size_t value)
{
    BUG_ON(str->size < value);
    str->text += value;
    str->size -= value;
}

static inline void str_extend_by(struct string *str, size_t value)
{
    BUG_ON(!str->text);
    str->size += value;
}

static inline void str_clear(struct string *str)
{
    str->text = nullptr;
    str->size = 0;
}

static inline bool str_pop_one(struct string *str, char *c)
{
    if (str_empty(*str))
        return false;

    *c = str->text[0];
    str_offset_by(str, 1);
    return true;
}

/*
 * Pops the token before the next separator, or the rest of the string if
 * there is none. Returns false once the string is exhausted. A trailing
 * separator yields an empty token after it.
 */
static inline bool str_pop_token(
    struct string *str, char separator, struct string *out_token
)
{
    ssize_t idx;

    /*
     * We must check for null explicitly because that's the only indicator
     * the string has been fully exhausted by str_clear() below. A non-null
     * but size == 0 string means there was a stray separator at the end
     * like "a,b,c,", which we must also propagate to the caller correctly.
     */
    if (str_is_null(*str))
        return false;

    idx = str_find_one(*str, separator, 0);
    if (idx < 0) {
        *out_token = *str;
        str_clear(str);
        return true;
    }

    *out_token = str_substring(*str, 0, idx);
    str_offset_by(str, idx + 1);
    return true;
}

static inline void str_terminated_copy(char *dst, struct string str)
{
    memcpy(dst, str.text, str.size);
    dst[str.size] = '\0';
}
