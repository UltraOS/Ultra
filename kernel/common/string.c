#include <common/string.h>

#undef memcpy
#undef memmove
#undef memset
#undef memcmp
#undef strlen
#undef strcmp
#undef strstr

/*
 * These functions don't have prototypes because they're invoked from the
 * respective __builtin_* intrinsic.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-prototypes"

void *memcpy(void *dest, const void *src, size_t count)
{
    char *cd = dest;
    const char *cs = src;

    while (count--)
        *cd++ = *cs++;

    return dest;
}

void *memmove(void *dest, const void *src, size_t count)
{
    char *cd = dest;
    const char *cs = src;

    if (src < dest) {
        cs += count;
        cd += count;

        while (count--)
            *--cd = *--cs;
    } else {
        memcpy(dest, src, count);
    }

    return dest;
}

void *memset(void *dest, int ch, size_t count)
{
    unsigned char fill = ch;
    unsigned char *cdest = dest;

    while (count--)
        *cdest++ = fill;

    return dest;
}

int memcmp(const void *lhs, const void *rhs, size_t count)
{
    const u8 *byte_lhs = lhs;
    const u8 *byte_rhs = rhs;
    size_t i;

    for (i = 0; i < count; ++i) {
        if (byte_lhs[i] != byte_rhs[i])
            return byte_lhs[i] - byte_rhs[i];
    }

    return 0;
}

size_t strlen(const char *str)
{
    const char *str1;

    for (str1 = str; *str1; str1++);

    return str1 - str;
}

char *strstr(const char *str, const char *sub_str)
{
    const char *cur, *needle_cur, *haystack_cur;

    if (*sub_str == '\0')
        return (char*)str;

    for (cur = str; *cur != '\0'; ++cur) {
        needle_cur = sub_str;
        haystack_cur = cur;

        while (*needle_cur != '\0' && *haystack_cur == *needle_cur) {
            ++haystack_cur;
            ++needle_cur;
        }

        if (*needle_cur == '\0')
            return (char*)cur;
    }

    return nullptr;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 != '\0' && *s1 == *s2) {
        ++s1;
        ++s2;
    }

    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

#pragma GCC diagnostic pop
