#pragma once

#include <stdarg.h>
#include <common/attributes.h>

NORETURN
void vpanic(const char *reason, va_list);

NORETURN
PRINTF_DECL(1, 2)
void panic(const char *reason, ...);
