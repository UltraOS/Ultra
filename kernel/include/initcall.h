#pragma once

#include <common/attributes.h>
#include <linker.h>

typedef int (*initcall_t)(void);

#ifndef ULTRA_RUNTIME_MODULE

extern initcall_t LINKER_SYMBOL(initcall_normal_begin)[];
extern initcall_t LINKER_SYMBOL(initcall_normal_end)[];

#define MAKE_INITCALL(func, type)                          \
    SECTION_VAR(initcall_##type, static const, initcall_t) \
    initcall_hook_##func = func

#define INITCALL(func) MAKE_INITCALL(func, normal)

#endif
