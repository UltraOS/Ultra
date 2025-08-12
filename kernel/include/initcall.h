#pragma once

#include <common/attributes.h>
#include <linker.h>

typedef int (*initcall_t)(void);

#ifndef ULTRA_RUNTIME_MODULE


extern initcall_t LINKER_SYMBOL(initcalls_normal_begin)[];
extern initcall_t LINKER_SYMBOL(initcalls_normal_end)[];

#define MAKE_INITCALL(func, type) \
    static initcall_t initcall_hook_##func USED SECTION(.initcall_##type) = func


// SMP up, memory management up
#define INITCALL(func)       MAKE_INITCALL(func, normal)

#endif
