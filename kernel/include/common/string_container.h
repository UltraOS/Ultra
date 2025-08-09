#pragma once

#include <common/types.h>
#include <common/string.h>

#include <bug.h>

struct string {
    union {
        const char *text;
        char *mutable_text;
    };
    size_t size;
};

#define STR_CONSTEXPR(str) (struct string) { { (str) }, sizeof((str)) - 1 }
#define STR(str)                                                    \
    __builtin_choose_expr(__builtin_constant_p((str)),              \
                          STR_CONSTEXPR((str)),                     \
                          (struct string) { { (str) }, strlen((str)) })

bool str_equals(struct string lhs, struct string rhs);
bool str_equals_caseless(struct string lhs, struct string rhs);
bool str_starts_with(struct string str, struct string prefix);
ssize_t str_find(struct string str, struct string needle, size_t starting_at);

static inline bool str_empty(struct string str)
{
    return str.size == 0;
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
    str->text = NULL;
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

static inline void str_terminated_copy(char *dst, struct string str)
{
    memcpy(dst, str.text, str.size);
    dst[str.size] = '\0';
}
