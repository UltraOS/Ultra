#pragma once

#include <memory/describe.h>

struct string per_cpu_describe(
    const void *addr, char out_desc_buf[static MAX_MEM_DESCRIPTION_LENGTH]
);
