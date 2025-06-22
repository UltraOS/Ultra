#pragma once

#include <arch/page_table.h>

struct address_space {
    struct pt5 *pt;
};
