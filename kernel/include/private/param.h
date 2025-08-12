#pragma once

#include <param.h>
#include <linker.h>

extern struct param SECTION_ARRAY_BEGIN(EARLY_PARAMETERS_SECTION)[];
extern struct param SECTION_ARRAY_END(EARLY_PARAMETERS_SECTION)[];

extern struct param SECTION_ARRAY_BEGIN(PARAMETERS_SECTION)[];
extern struct param SECTION_ARRAY_END(PARAMETERS_SECTION)[];
