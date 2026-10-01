#pragma once

#include <common/types.h>

enum vm_prot : u32 {
    VM_PROT_NONE = 0,
    VM_PROT_READ = 1 << 0,
    VM_PROT_WRITE = 1 << 1,
    VM_PROT_EXEC = 1 << 2,
    VM_PROT_KERNEL = 1 << 3,
};
