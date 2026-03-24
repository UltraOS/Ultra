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

#define FREE_AFTER_INIT_SECTION free_after_init
#define FREE_AFTER_INIT_TEXT_SECTION FREE_AFTER_INIT_SECTION.text
#define FREE_AFTER_INIT_RODATA_SECTION FREE_AFTER_INIT_SECTION.rodata
#define FREE_AFTER_INIT_DATA_SECTION FREE_AFTER_INIT_SECTION.data

#define INIT_DATA_REFERENCE_TEXT_SECTION text.init_data_reference
#define INIT_DATA_REFERENCE_DATA_SECTION data.init_data_reference
#define INIT_DATA_REFERENCE_RODATA_SECTION rodata.init_data_reference
