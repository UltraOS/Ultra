#pragma once

#include <common/types.h>
#include <common/string_container.h>

#define MAX_MEM_DESCRIPTION_LENGTH 128

/*
 * Describe the virtual address pointed to by 'addr' to the best of the kernel's
 * knowledge. This queries every possible subsystem to see if it's able to
 * recognize the address as something it is responsible for and produce a short
 * description of what it points at.
 */
struct string mem_describe(
    const void *addr, char out_desc_buf[static MAX_MEM_DESCRIPTION_LENGTH]
);
