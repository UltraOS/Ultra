#pragma once

#include <common/attributes.h>

typedef int (*initcall_t)(void);

#ifndef ULTRA_RUNTIME_MODULE

extern initcall_t g_linker_symbol_initcalls_earlycon_begin[];
extern initcall_t g_linker_symbol_initcalls_earlycon_end[];

extern initcall_t g_linker_symbol_initcalls_normal_begin[];
extern initcall_t g_linker_symbol_initcalls_normal_end[];

#define MAKE_INITCALL(func, type) \
    static initcall_t initcall_hook_##func USED SECTION(.initcall_##type) = func

// No SMP, no memory management, purely used for registering an early console
#define INITCALL_EARLYCON(func) MAKE_INITCALL(func, earlycon)

// SMP up, memory management up
#define INITCALL(func)       MAKE_INITCALL(func, normal)

#endif
