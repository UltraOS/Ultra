#include <common/types.h>

/*
 * These functions don't have prototypes because they're invoked from the
 * respective __builtin_* intrinsic.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-prototypes"

void *memcpy(void *dest, const void *src, size_t count)
{
    void *ret = dest;
    size_t qwords, bytes;

    qwords = count >> 3;
    bytes = count & 7;

    asm volatile (
        "rep movsq" : "+D"(dest), "+S"(src), "+c"(qwords) :: "memory"
    );

    if (bytes) {
        asm volatile (
            "rep movsb" : "+D"(dest), "+S"(src), "+c"(bytes) :: "memory"
        );
    }

    return ret;
}

void *memset(void *dest, int ch, size_t count)
{
    void *ret = dest;
    u64 pattern;
    size_t qwords, bytes;

    pattern = 0x0101010101010101ULL * (u8)ch;
    qwords = count >> 3;
    bytes = count & 7;

    asm volatile (
        "rep stosq" : "+D"(dest), "+c"(qwords) : "a"(pattern) : "memory"
    );

    if (bytes) {
        asm volatile (
            "rep stosb" : "+D"(dest), "+c"(bytes) : "a"(pattern) : "memory"
        );
    }

    return ret;
}

#pragma GCC diagnostic pop
