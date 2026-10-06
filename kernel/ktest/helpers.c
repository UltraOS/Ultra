#include <private/ktest/helpers.h>

bool ktest_case_is_selected(
    struct string suite, struct string name, struct string selection
)
{
    struct string token, token_suite, token_name;
    ssize_t dot;

    /*
     * 'selection' is a comma separated list of the following:
     *     "all" - runs all registered tests
     *     "suite" - runs all cases of 'suite'
     *     "suite.case" - runs a specific test 'case' of 'suite'
     */
    while (str_pop_token(&selection, ',', &token)) {
        if (str_equals(token, STR("all")))
            return true;

        dot = str_find_one(token, '.', 0);
        if (dot < 0) {
            if (str_equals(token, suite))
                return true;

            continue;
        }

        token_suite = str_substring(token, 0, dot);
        token_name = str_substring(token, dot + 1, token.size);

        if (str_equals(token_suite, suite) &&
            str_equals(token_name, name))
            return true;
    }

    return false;
}
