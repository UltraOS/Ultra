#pragma once

#include <initcall.h>
#include <common/attributes.h>

#ifdef ULTRA_RUNTIME_MODULE

// Alias these so that the module loader can always find them
#define MODULE_INIT(func) error_t module_init(void) ALIAS_OF(func)
#define MODULE_FINI(func) error_t module_fini(void) ALIAS_OF(func)

#define INITCALL_EARLYCON(func) MODULE_INIT(func)
#define INITCALL_NORMAL(func) MODULE_INIT(func)

#else

#define MODULE_INIT(func) INITCALL_NORMAL(func)
#define MODULE_FINI(func) \
    static UNUSED_DECL error_t module_fini(void) ALIAS_OF(func)

#endif
