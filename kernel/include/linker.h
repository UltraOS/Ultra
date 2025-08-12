#pragma once

#include <common/helpers.h>

#define LINKER_SYMBOL(x) CONCAT(g_linker_symbol_, x)

#define SECTION_ARRAY_BEGIN(x) CONCAT(LINKER_SYMBOL(x), _begin)
#define SECTION_ARRAY_END(x) CONCAT(LINKER_SYMBOL(x), _end)
#define SECTION_ARRAY_SIZE(x) (SECTION_ARRAY_END(x) - SECTION_ARRAY_BEGIN(x))

#define SECTION_ARRAY_ARGS(x) \
    SECTION_ARRAY_BEGIN(x), SECTION_ARRAY_SIZE(x)

#define EARLY_PARAMETERS_SECTION early_parameters
#define PARAMETERS_SECTION parameters
