#pragma once

#include <common/attributes.h>
#include <panic.h>

#define BUG() panic("BUG! At %s() in file %s:%d\n", __func__, __FILE__, __LINE__)

#define BUG_ON(expr)        \
    do {                    \
        if (unlikely(expr)) \
            BUG();          \
    } while (0)
