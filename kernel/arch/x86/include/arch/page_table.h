#pragma once

#include <common/types.h>
#include <arch/constants.h>
#include <memory/vm_flags.h>
#include <io.h>

#define X86_MAX_PHYS_BITS 52
#define X86_PHYS_MASK ((1ull << X86_MAX_PHYS_BITS) - 1)

#define X86_PT_PRESENT (1ull << 0)
#define X86_PT_WRITE (1ull << 1)
#define X86_PT_USER (1ull << 2)
#define X86_PT_ACCESSED (1ull << 5)
#define X86_PT_DIRTY (1ull << 6)
#define X86_PT_HUGE (1ull << 7)
#define X86_PT_GLOBAL (1ull << 8)
#define X86_PT_NX (1ull << 63)

#define X86_PT_MASK (X86_PT_PRESENT | X86_PT_WRITE | X86_PT_USER)

/*
 * Intel® Xeon Phi™ Processor x200 Product Family (KNL4):
 *     The A (Accessed, bit 5) and/or D (Dirty, bit 6) bits in a
 *     paging-structure entry (e.g., a Page-Table Entry) may be set to 1 even
 *     when that entry has its Present bit cleared or has a reserved bit set.
 *     This can only occur when one logical processor has cleared the Present
 *     bit or set a reserved bit in a paging-structure entry, while at the same
 *     time another logical processor accesses the contents of a linear address
 *     mapped by that entry.
 */
#define X86_KNL4_ERRATUM_MASK (X86_PT_ACCESSED | X86_PT_DIRTY)

#define MAKE_X86_PT_HELPERS(idx)                                    \
    static inline bool pt##idx##_present(struct pt##idx *pt)        \
    {                                                               \
        return pt->value & X86_PT_PRESENT;                          \
    }                                                               \
                                                                    \
    static inline void *pt##idx##_to_virt(struct pt##idx *pt)       \
    {                                                               \
        phys_addr_t phys_addr;                                      \
                                                                    \
        phys_addr = pt->value & CONCAT(CONCAT(PT, idx), _PFN_MASK); \
        return phys_to_virt(phys_addr);                             \
    }                                                               \
    static inline bool pt##idx##_none(struct pt##idx *pt)           \
    {                                                               \
        return (pt->value & ~X86_KNL4_ERRATUM_MASK) == 0;           \
    }

#define MAKE_X86_PT_POPULATE(idx, idx_minus_one)                    \
    static inline void pt##idx##_populate(                          \
        struct pt##idx *parent, struct pt##idx_minus_one *child)    \
    {                                                               \
        parent->value = virt_to_phys(child) | X86_PT_MASK;          \
    }


struct pt_prot { u64 value; };
struct pt_prot pt_prot_from_vm_prot(enum vm_prot);

#define MAKE_X86_PT_TYPE(idx) struct pt##idx { u64 value; };

MAKE_X86_PT_TYPE(1)

static inline void pt1_populate(
    struct pt1 *parent, phys_addr_t phys_addr, struct pt_prot prot
)
{
    parent->value = phys_addr | prot.value;
}

MAKE_X86_PT_TYPE(2)
MAKE_X86_PT_POPULATE(2, 1)

MAKE_X86_PT_TYPE(3)
MAKE_X86_PT_POPULATE(3, 2)

MAKE_X86_PT_TYPE(4)
MAKE_X86_PT_POPULATE(4, 3)

MAKE_X86_PT_TYPE(5)

// Set dynamically based on LA57 support
extern bool g_la57;
extern u64 g_pt5_shift;
extern u64 g_pt4_num_entries;

#define X86_PT_LVL_SHIFT 9
#define X86_PT_LVL_ENTRIES (1 << X86_PT_LVL_SHIFT)

#define PT1_SHIFT (PAGE_SHIFT)
#define PT1_NUM_ENTRIES X86_PT_LVL_ENTRIES

#define PT2_SHIFT (PT1_SHIFT + X86_PT_LVL_SHIFT)
#define PT2_NUM_ENTRIES X86_PT_LVL_ENTRIES

#define PT3_SHIFT (PT2_SHIFT + X86_PT_LVL_SHIFT)
#define PT3_NUM_ENTRIES X86_PT_LVL_ENTRIES

#define PT4_SHIFT (PT3_SHIFT + X86_PT_LVL_SHIFT)
#define PT4_NUM_ENTRIES g_pt4_num_entries

#define PT5_SHIFT g_pt5_shift
#define PT5_NUM_ENTRIES X86_PT_LVL_ENTRIES

#define X86_PAGE_MASK (X86_PHYS_MASK & (~(PAGE_SIZE - 1ull)))
#define X86_PT3_MASK (X86_PHYS_MASK & (~((1ull << PT3_SHIFT) - 1)))
#define X86_PT2_MASK (X86_PHYS_MASK & (~((1ull << PT2_SHIFT) - 1)))

#define PT5_PFN_MASK X86_PAGE_MASK
#define PT4_PFN_MASK X86_PAGE_MASK
#define PT3_PFN_MASK X86_PT3_MASK
#define PT2_PFN_MASK X86_PT2_MASK
#define PT1_PFN_MASK X86_PAGE_MASK

static inline void pt5_populate(struct pt5 *parent, struct pt4 *child)
{
    if (!g_la57)
        return;

    parent->value = virt_to_phys(child) | X86_PT_MASK;
}

static inline bool pt5_present(struct pt5 *pt)
{
    if (!g_la57)
        return true;

    return pt->value & X86_PT_PRESENT;
}

static inline bool pt5_none(struct pt5 *pt)
{
    if (!g_la57)
        return false;

    return (pt->value & ~X86_KNL4_ERRATUM_MASK) == 0;
}

static inline void *pt5_to_virt(struct pt5 *pt)
{
    phys_addr_t phys_addr;

    phys_addr = pt->value & PT5_PFN_MASK;
    return phys_to_virt(phys_addr);
}

MAKE_X86_PT_HELPERS(4)
MAKE_X86_PT_HELPERS(3)
MAKE_X86_PT_HELPERS(2)
MAKE_X86_PT_HELPERS(1)

static inline struct pt1 pt1_make_writeable(struct pt1 pt)
{
    pt.value |= X86_PT_WRITE;
    return pt;
}

#define ARCH_HAS_CUSTOM_PT4_FROM_PT5
struct pt4 *pt4_from_pt5(struct pt5 *pt5, virt_addr_t addr);
