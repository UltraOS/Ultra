#pragma once

#include <log.h>
#include <panic.h>

#define BUG() panic("BUG at %s:%d", __FILE__, __LINE__)

#ifdef ULTRA_DEADLY_WARNINGS
#define WARN() panic("WARNING at %s:%d", __FILE__, __LINE__)
#else
#define WARN() pr_warn("WARNING at %s:%d\n", __FILE__, __LINE__)
#endif
