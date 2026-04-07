#pragma once

#include <init_level.h>
#include <common/attributes.h>

#ifdef ULTRA_RUNTIME_MODULE

// Alias these so that the module loader can always find them
#define MODULE_INIT(func) error_t module_init(void) ALIAS_OF(func)
#define MODULE_FINI(func) error_t module_fini(void) ALIAS_OF(func)

#define INIT_CALL_PRE(level, func) MODULE_INIT(func)
#define INIT_CALL_POST(level, func) MODULE_INIT(func)

#else

#define MODULE_INIT(func) INIT_CALL_PRE(GENERIC_MODULES_INITIALIZED, func)
#define MODULE_FINI(func) \
    static UNUSED_DECL error_t module_fini(void) ALIAS_OF(func)

#endif
