#include <common/string_container.h>
#include <common/ctype.h>

bool str_equals(struct string lhs, struct string rhs)
{
    size_t i;

    if (lhs.size != rhs.size)
        return false;

    for (i = 0; i < lhs.size; ++i) {
        if (lhs.text[i] != rhs.text[i])
            return false;
    }

    return true;
}

bool str_equals_caseless(struct string lhs, struct string rhs)
{
    size_t i;

    if (lhs.size != rhs.size)
        return false;

    for (i = 0; i < lhs.size; ++i) {
        if (tolower(lhs.text[i]) != tolower(rhs.text[i]))
            return false;
    }

    return true;
}

bool str_starts_with(struct string str, struct string prefix)
{
    size_t i;

    if (prefix.size > str.size)
        return false;
    if (prefix.size == 0)
        return true;

    for (i = 0; i < prefix.size; ++i) {
        if (str.text[i] != prefix.text[i])
            return false;
    }

    return true;
}

ssize_t str_find(struct string str, struct string needle, size_t starting_at)
{
    size_t i, j, k;

    BUG_ON(starting_at > str.size);

    if (needle.size > (str.size - starting_at))
        return -1;
    if (str_empty(needle))
        return starting_at;

    for (i = starting_at; i < str.size - needle.size + 1; ++i) {
        if (str.text[i] != needle.text[0])
            continue;

        j = i;
        k = 0;

        while (k < needle.size) {
            if (str.text[j++] != needle.text[k])
                break;

            k++;
        }

        if (k == needle.size)
            return i;
    }

    return -1;
}
