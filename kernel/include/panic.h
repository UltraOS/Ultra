#pragma once

#include <common/attributes.h>

PRINTF_DECL(1, 2)
void panic(const char *reason, ...);
