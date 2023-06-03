#include <common/string.h>

#undef memcpy
#undef memmove
#undef memset
#undef memcmp
#undef strlen

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
