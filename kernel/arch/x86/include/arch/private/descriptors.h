#pragma once

#define DESC_SIZE 8

#define DESC_AFTER(x) ((x) + DESC_SIZE)
#define DESC_IDX(x) ((x) / DESC_SIZE)

// Layout mandated by SYSCALL/SYSRET
#define KERNEL_CS DESC_AFTER(0)
#define KERNEL_SS DESC_AFTER(KERNEL_CS)
#define USER_CS_COMPAT DESC_AFTER(KERNEL_SS)
#define USER_SS DESC_AFTER(USER_CS_COMPAT)
#define USER_CS DESC_AFTER(USER_SS)

// No TSS for now, add when we get there
#define NUM_GDT_ENTRIES DESC_IDX(DESC_AFTER(USER_CS))

#ifndef __ASSEMBLER__

#include <common/types.h>
#include <common/attributes.h>

typedef u64 descriptor_t;
BUILD_BUG_ON(sizeof(descriptor_t) != DESC_SIZE);

struct descriptor_ptr {
     u16 limit;
     u64 base;
} PACKED;
BUILD_BUG_ON(sizeof(struct descriptor_ptr) != 10);

#define DESC_PRESENT (1ull << 47)
#define DESC_DPL(x) (((descriptor_t)(x)) << 45)

// Only applicable for segment descriptors
#define DESC_ACCESSED (1ull << 40)
#define DESC_DATA_WRITABLE (1ull << 41)
#define DESC_CODE_READABLE (1ull << 41)
#define DESC_EXECUTABLE (1ull << 43)
#define DESC_TSS ((1ull << 40) | (1ull << 43))
#define DESC_CODE_OR_DATA (1ull << 44)
#define DESC_64 (1ull << 53)
#define DESC_DEFAULT (1ull << 54)
#define DESC_BIG (1ull << 54)
#define DESC_GRANULARITY_4K (1ull << 55)

// Only applicable for interrupt descriptors
#define DESC_IST(x) (((descriptor_t)(x)) << 32)
#define DESC_INTERRUPT_GATE (0b1110ull << 40)

#define SEGMENT_LIMIT_MAX ((1ull << 20) - 1)

#define SEGMENT_LIMIT0(x) (((x) & 0xFFFF) << 0)
#define SEGMENT_LIMIT1(x) (((x) & 0xF) << 48)

#define SEGMENT_DESCRIPTOR(flags) ((descriptor_t)( \
     SEGMENT_LIMIT0(SEGMENT_LIMIT_MAX) |           \
     SEGMENT_LIMIT1(SEGMENT_LIMIT_MAX) |           \
     flags                                         \
))

#define SEGMENT_CODE_OR_DATA_TEMPLATE (                                     \
     DESC_PRESENT | DESC_CODE_OR_DATA | DESC_ACCESSED | DESC_GRANULARITY_4K \
)

#define SEGMENT_KERNEL_CODE_TEMPLATE (                                      \
     SEGMENT_CODE_OR_DATA_TEMPLATE | DESC_CODE_READABLE | DESC_EXECUTABLE   \
)
#define SEGMENT_KERNEL_CODE32 SEGMENT_DESCRIPTOR( \
     SEGMENT_KERNEL_CODE_TEMPLATE | DESC_DEFAULT  \
)
#define SEGMENT_KERNEL_CODE64 SEGMENT_DESCRIPTOR( \
     SEGMENT_KERNEL_CODE_TEMPLATE | DESC_64       \
)

#define SEGMENT_KERNEL_DATA32 SEGMENT_DESCRIPTOR(                  \
     SEGMENT_CODE_OR_DATA_TEMPLATE | DESC_DATA_WRITABLE | DESC_BIG \
)
#define SEGMENT_KERNEL_DATA64 SEGMENT_KERNEL_DATA32

#define SEGMENT_USER_CODE32 SEGMENT_DESCRIPTOR( \
     SEGMENT_KERNEL_CODE32 | DESC_DPL(3)        \
)
#define SEGMENT_USER_CODE64 SEGMENT_DESCRIPTOR( \
     SEGMENT_KERNEL_CODE64 | DESC_DPL(3)        \
)
#define SEGMENT_USER_DATA64 SEGMENT_DESCRIPTOR( \
     SEGMENT_KERNEL_DATA64 | DESC_DPL(3)        \
)

#define INTERRUPT_DESCRIPTOR_LOW(addr, ist, dpl) ((descriptor_t)( \
     (KERNEL_CS << 16) | ((addr) & 0xFFFF) | DESC_IST(ist) |      \
     DESC_INTERRUPT_GATE | DESC_DPL(dpl) | DESC_PRESENT    |      \
     ((((addr) >> 16) & 0xFFFF) << 48)                            \
))
#define INTERRUPT_DESCRIPTOR_HIGH(addr) ((descriptor_t)((addr) >> 32))

void load_gdt(struct descriptor_ptr *ptr);

#endif
