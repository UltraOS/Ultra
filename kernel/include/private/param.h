#pragma once

#include <param.h>
#include <linker.h>

DECLARE_TYPED_SECTION_MARKERS(struct param, PARAMETERS_SECTION);
DECLARE_TYPED_SECTION_MARKERS(struct param, FREE_AFTER_INIT_PARAMETERS_SECTION);
