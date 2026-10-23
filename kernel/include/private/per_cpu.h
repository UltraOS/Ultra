#pragma once

#include <memory/describe.h>

int per_cpu_describe(
    const void *addr, char out_desc_buf[MAX_MEM_DESCRIPTION_LENGTH]
);
