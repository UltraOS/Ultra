#pragma once

#include <common/helpers.h>

#define LINKER_SYMBOL(x) CONCAT(g_linker_symbol_, x)
#define DECLARE_LINKER_SYMBOL(x) extern u8 LINKER_SYMBOL(x)[]
#define LINKER_SYMBOL_ADDR(x) ((ptr_t)LINKER_SYMBOL(x))

#define SECTION_NAME(sec) TO_STR(.##sec)

#define SECTION_MARKER_BEGIN(x) CONCAT(LINKER_SYMBOL(x), _begin)
#define SECTION_MARKER_END(x) CONCAT(LINKER_SYMBOL(x), _end)
#define SECTION_MARKER_BEGIN_ADDR(x) ((ptr_t)SECTION_MARKER_BEGIN(x))
#define SECTION_MARKER_END_ADDR(x) ((ptr_t)SECTION_MARKER_END(x))
#define SECTION_SIZE(x) (SECTION_MARKER_END(x) - SECTION_MARKER_BEGIN(x))

#define DECLARE_TYPED_SECTION_MARKERS(type, x) \
    extern type SECTION_MARKER_BEGIN(x)[];     \
    extern type SECTION_MARKER_END(x)[]

#define DECLARE_SECTION_MARKERS(x) DECLARE_TYPED_SECTION_MARKERS(u8, x)

#define SECTION_ARRAY_BEGIN(x) SECTION_MARKER_BEGIN(x)
#define SECTION_ARRAY_END(x) SECTION_MARKER_END(x)
#define SECTION_ARRAY_SIZE(x) ((size_t)SECTION_SIZE(x))

#define SECTION_ARRAY_ARGS(x) \
    SECTION_ARRAY_BEGIN(x), SECTION_ARRAY_SIZE(x)

#define for_each_section_array_item(item, x) \
    for ((item) = SECTION_ARRAY_BEGIN(x);    \
         (item) < SECTION_ARRAY_END(x);      \
         (item)++)

#define ABORTABLE_INSTRUCTIONS_SECTION abortable_instructions
#define BUG_TABLE_SECTION bug_table
#define PARAMETERS_SECTION parameters
#define KTEST_CASES_SECTION ktest_cases
#define PER_CPU_SECTION per_cpu

#define FREE_AFTER_INIT_SECTION free_after_init
#define FREE_AFTER_INIT_TEXT_SECTION CONCAT(FREE_AFTER_INIT_SECTION, _text)
#define FREE_AFTER_INIT_RODATA_SECTION CONCAT(FREE_AFTER_INIT_SECTION, _rodata)
#define FREE_AFTER_INIT_DATA_SECTION CONCAT(FREE_AFTER_INIT_SECTION, _data)
#define FREE_AFTER_INIT_PARAMETERS_SECTION \
    CONCAT(FREE_AFTER_INIT_SECTION, _parameters)

#define INIT_DATA_REFERENCE_TEXT_SECTION text.init_data_reference
#define INIT_DATA_REFERENCE_DATA_SECTION data.init_data_reference
#define INIT_DATA_REFERENCE_RODATA_SECTION rodata.init_data_reference
