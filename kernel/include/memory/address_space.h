#pragma once

#include <memory/page_table.h>

struct address_space {
    pt_root *pt;
};

extern struct address_space g_kernel_address_space;
