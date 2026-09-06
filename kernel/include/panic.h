#pragma once

#include <stdarg.h>

#include <common/attributes.h>
#include <common/helpers.h>

NORETURN
PRINTF_DECL(1, 2)
void panic(const char *reason, ...);

#define panic(msg, ...) do {                                         \
        BUILD_BUG_ON_WITH_MSG(                                       \
            __builtin_memcmp((msg) + sizeof(msg) - 2, "\n", 1) == 0, \
            "Please don't use newlines in panic messages"            \
        );                                                           \
        panic((msg) __VA_OPT__(,) __VA_ARGS__);                      \
    } while (0)
