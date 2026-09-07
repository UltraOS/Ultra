#pragma once

#include <param.h>
#include <linker.h>

extern struct param SECTION_ARRAY_BEGIN(PARAMETERS_SECTION)[];
extern struct param SECTION_ARRAY_END(PARAMETERS_SECTION)[];

extern struct param SECTION_ARRAY_BEGIN(FREE_AFTER_INIT_PARAMETERS_SECTION)[];
extern struct param SECTION_ARRAY_END(FREE_AFTER_INIT_PARAMETERS_SECTION)[];
