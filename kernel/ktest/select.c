#include <private/ktest/select.h>

bool ktest_case_is_selected(
    const char *suite, const char *name, struct string run
)
{
    struct string token, token_suite, token_name;
    ssize_t dot;

    while (str_pop_token(&run, ',', &token)) {
        if (str_equals(token, STR("all")))
            return true;

        dot = str_find_one(token, '.', 0);
        if (dot < 0) {
            if (str_equals(token, STR_RUNTIME(suite)))
                return true;

            continue;
        }

        token_suite = str_substring(token, 0, dot);
        token_name = str_substring(token, dot + 1, token.size);

        if (str_equals(token_suite, STR_RUNTIME(suite)) &&
            str_equals(token_name, STR_RUNTIME(name)))
            return true;
    }

    return false;
}
