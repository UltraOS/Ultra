#pragma once

#ifndef ULTRA_TEST

#include <common/types.h>

enum char_type {
    CHAR_TYPE_CONTROL = 1 << 0,
    CHAR_TYPE_SPACE = 1 << 1,
    CHAR_TYPE_BLANK = 1 << 2,
    CHAR_TYPE_PUNCTUATION = 1 << 3,
    CHAR_TYPE_LOWER = 1 << 4,
    CHAR_TYPE_UPPER = 1 << 5,
    CHAR_TYPE_DIGIT = 1 << 6,
    CHAR_TYPE_HEX_DIGIT  = 1 << 7,
    CHAR_TYPE_ALPHA = CHAR_TYPE_LOWER | CHAR_TYPE_UPPER,
    CHAR_TYPE_ALHEX = CHAR_TYPE_ALPHA | CHAR_TYPE_HEX_DIGIT,
    CHAR_TYPE_ALNUM = CHAR_TYPE_ALPHA | CHAR_TYPE_DIGIT,
};

extern const u8 g_ascii_map[256];

static inline bool is_char_of_type(char c, enum char_type type)
{
    return (g_ascii_map[(u8)c] & type) == type;
}

static inline bool isupper(char c)
{
    return is_char_of_type(c, CHAR_TYPE_UPPER);
}

static inline bool islower(char c)
{
    return is_char_of_type(c, CHAR_TYPE_LOWER);
}

static inline bool isalnum(char c)
{
    return is_char_of_type(c, CHAR_TYPE_ALNUM);
}

static inline bool isspace(char c)
{
    return is_char_of_type(c, CHAR_TYPE_SPACE);
}

static inline bool isdigit(char c)
{
    return is_char_of_type(c, CHAR_TYPE_DIGIT);
}

static inline bool isxdigit(char c)
{
    return is_char_of_type(c, CHAR_TYPE_HEX_DIGIT);
}

#define CHAR_LOWER_TO_UPPER_OFFSET ('a' - 'A')
BUILD_BUG_ON(CHAR_LOWER_TO_UPPER_OFFSET < 0);

static inline char tolower(char c)
{
    if (isupper(c))
        return c + CHAR_LOWER_TO_UPPER_OFFSET;

    return c;
}

static inline char toupper(char c)
{
    if (islower(c))
        return c - CHAR_LOWER_TO_UPPER_OFFSET;

    return c;
}

#else // ULTRA_TEST

#include <ctype.h>

#endif
