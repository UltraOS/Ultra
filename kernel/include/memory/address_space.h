#pragma once

#include <memory/page_table.h>

struct address_space {
    pt_root *pt;
};
