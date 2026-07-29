#pragma once

#include <config.h>
#include <bug.h>

#if IS_ENABLED(DEBUG_MM)
#define MM_BUG_ON(x) BUG_ON(x)
#else
#define MM_BUG_ON(x) do { if (0) { (void)(x); } } while (0)
#endif
