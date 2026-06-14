#pragma once

/*
 * The way the IS_DEFINED (and CONFIG_{OR,AND}) checks work:
 * 1. IS_DEFINED takes an option and simply expands it.
 *    The following two cases are possible for IS_DEFINED(CONFIG_FOO):
 *        a. If enabled -> IS_DEFINED_EXPANDED_OPTION(1)
 *        b. if disabled -> IS_DEFINED_EXPANDED_OPTION(CONFIG_FOO)
 * 2. IS_DEFINED_EXPANDED_OPTION concatenates the result of the expansion
 *    with CONFIG_EXPANSION_CHECK_. Recall the two outcomes from step 1,
 *    as a result we have the following cases:
 *       a. If enabled -> CONFIG_EXPANSION_CHECK_##1
 *       b. If disabled -> CONFIG_EXPANSION_CHECK_##CONFIG_FOO
 * 3. CONFIG_EXPANSION_CHECK_PASSED expands the result of the previous step
 *    by passing it into TAKE_SECOND_ARG. The following two cases are
 *    possible:
 *      a. If enabled -> TAKE_SECOND_ARG(CHECK_PASSED, 1, 0)
 *      b. If disabled -> TAKE_SECOND_ARG(CONFIG_EXPANSION_CHECK_CONFIG_FOO 1, 0)
 * 4. As an outcome from step 3, TAKE_SECOND_ARG simply takes 1 if the check
 *    passed, and 0 otherwise.
 */
#define CONFIG_EXPANSION_CHECK_1 CHECK_PASSED,
#define CONFIG_EXPANSION_CHECK_PASSED(x, y) TAKE_SECOND_ARG(x 1, y)

#define IS_DEFINED_EXPANDED_OPTION(option) \
    CONFIG_EXPANSION_CHECK_PASSED(CONFIG_EXPANSION_CHECK_##option, 0)
#define IS_DEFINED(option) IS_DEFINED_EXPANDED_OPTION(option)

#define CONFIG_OR_EXPANDED(x, y) \
    CONFIG_EXPANSION_CHECK_PASSED(CONFIG_EXPANSION_CHECK_##x, y)
#define CONFIG_OR(x, y) CONFIG_OR_EXPANDED(x, y)

#define CONFIG_AND_CHECK_PASSED(x, y) TAKE_SECOND_ARG(x y, 0)
#define CONFIG_AND_EXPANDED(x, y) \
    CONFIG_AND_CHECK_PASSED(CONFIG_EXPANSION_CHECK_##x, y)
#define CONFIG_AND(x, y) CONFIG_AND_EXPANDED(x, y)

#define IS_BUILTIN(option) IS_DEFINED(CONFIG_##option)
#define IS_MODULE(option) IS_DEFINED(CONFIG_##option##_MODULE)
#define IS_ENABLED(option) CONFIG_OR(IS_BUILTIN(option), IS_MODULE(option))
#define IS_DISABLED(option) !IS_ENABLED(option)

/*
 * Check if this piece of code is allowed to call into or use variables from
 * code that is enabled by 'option'.
 */
#define IS_REACHABLE(option)                                            \
    CONFIG_OR(                                                          \
        IS_BUILTIN(option),                                             \
        CONFIG_AND(IS_MODULE(option), IS_DEFINED(ULTRA_RUNTIME_MODULE)) \
    )
