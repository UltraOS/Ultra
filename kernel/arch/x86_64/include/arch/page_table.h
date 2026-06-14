#pragma once

#include <common/types.h>
#include <common/bit.h>

#include <arch/constants.h>
#include <memory/vm_flags.h>
#include <memory/io.h>

#define X86_MAX_PHYS_BITS 52
#define X86_PHYS_MASK MAKE_BIT_MASK(X86_MAX_PHYS_BITS - 1, 0)

#define X86_PT_PRESENT BIT(0)
#define X86_PT_WRITE BIT(1)
#define X86_PT_USER BIT(2)
#define X86_PT_WRITETHROUGH BIT(3)
#define X86_PT_UNCACHED BIT(4)
#define X86_PT_ACCESSED BIT(5)
#define X86_PT_DIRTY BIT(6)
#define X86_PT_HUGE BIT(7)
#define X86_PT_GLOBAL BIT(8)
#define X86_PT_NX BIT(63)

#define X86_SMALL_PAT_SHIFT 7
#define X86_HUGE_PAT_SHIFT 12
#define X86_PT_SMALL_PAT BIT(X86_SMALL_PAT_SHIFT)
#define X86_PT_HUGE_PAT BIT(X86_HUGE_PAT_SHIFT)

#define MAKE_X86_PT_TYPE(idx) struct pt##idx { u64 value; };

// Raw arch-specific page table entry flags
typedef struct pt_prot { u64 value; } pt_prot;

static inline pt_prot io_window_pt_prot(pt_prot prot)
{
    // Strong UC mode
    prot.value |= X86_PT_UNCACHED | X86_PT_WRITETHROUGH;
    return prot;
}

#define ARCH_HAS_IO_WINDOW_WT_PT_PROT
static inline pt_prot io_window_wt_pt_prot(pt_prot prot)
{
    prot.value |= X86_PT_WRITETHROUGH;
    return prot;
}

extern u64 g_wc_pt_prot;

#define ARCH_HAS_IO_WINDOW_WC_PT_PROT
static inline pt_prot io_window_wc_pt_prot(pt_prot prot)
{
    prot.value |= g_wc_pt_prot;
    return prot;
}

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

#define MAKE_X86_PT_TO_VIRT(idx)                              \
                                                              \
    static inline void *pt##idx##_to_virt(struct pt##idx *pt) \
    {                                                         \
        phys_addr_t phys_addr;                                \
                                                              \
        phys_addr = pt->value & X86_PAGE_MASK;                \
        return phys_to_virt(phys_addr);                       \
    }

#define MAKE_X86_PT_HELPERS(idx)                             \
    static inline bool pt##idx##_present(struct pt##idx *pt) \
    {                                                        \
        return pt->value & X86_PT_PRESENT;                   \
    }                                                        \
                                                             \
    static inline bool pt##idx##_none(struct pt##idx *pt)    \
    {                                                        \
        return (pt->value & ~X86_KNL4_ERRATUM_MASK) == 0;    \
    }                                                        \
                                                             \
    static inline void pt##idx##_clear(struct pt##idx *pt)   \
    {                                                        \
        pt->value = 0;                                       \
    }

// Mask that is used for all intermediate page-table levels
#define X86_PT_MASK (X86_PT_PRESENT | X86_PT_WRITE | X86_PT_USER)

#define MAKE_X86_PT_POPULATE(idx, idx_minus_one)                 \
    static inline void pt##idx##_populate(                       \
        struct pt##idx *parent, struct pt##idx_minus_one *child) \
    {                                                            \
        parent->value = virt_to_phys(child) | X86_PT_MASK;       \
    }

#define MAKE_X86_PT_LEAF_HELPERS(idx, can_be_leaf_ret)       \
    static inline bool pt##idx##_can_be_leaf(void)           \
    {                                                        \
        return can_be_leaf_ret;                              \
    }                                                        \
                                                             \
    static inline bool pt##idx##_is_leaf(struct pt##idx *pt) \
    {                                                        \
        return pt->value & X86_PT_HUGE;                      \
    }

MAKE_X86_PT_TYPE(1)

static inline void pt1_populate(
    struct pt1 *parent, phys_addr_t phys_addr, pt_prot prot
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

#define ARCH_HAS_CUSTOM_PT5_IS_FOLDED
static inline bool pt5_is_folded(void)
{
    return !g_la57;
}

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

#define X86_PAGE_MASK MAKE_BIT_MASK(X86_MAX_PHYS_BITS - 1, PAGE_SHIFT)
#define X86_PT3_MASK  MAKE_BIT_MASK(X86_MAX_PHYS_BITS - 1, PT3_SHIFT)
#define X86_PT2_MASK  MAKE_BIT_MASK(X86_MAX_PHYS_BITS - 1, PT2_SHIFT)

static inline void pt5_populate(struct pt5 *parent, struct pt4 *child)
{
    if (pt5_is_folded())
        return;

    parent->value = virt_to_phys(child) | X86_PT_MASK;
}

static inline bool pt5_present(struct pt5 *pt)
{
    if (pt5_is_folded())
        return true;

    return pt->value & X86_PT_PRESENT;
}

static inline bool pt5_none(struct pt5 *pt)
{
    if (pt5_is_folded())
        return false;

    return (pt->value & ~X86_KNL4_ERRATUM_MASK) == 0;
}

static inline void *pt5_to_virt(struct pt5 *pt)
{
    phys_addr_t phys_addr;

    phys_addr = pt->value & X86_PAGE_MASK;
    return phys_to_virt(phys_addr);
}

MAKE_X86_PT_HELPERS(4)
MAKE_X86_PT_TO_VIRT(4)

MAKE_X86_PT_HELPERS(3)

extern bool g_have_gb_pages;

#define ARCH_HAS_CUSTOM_PT3_LEAF
MAKE_X86_PT_LEAF_HELPERS(3, g_have_gb_pages)

static inline pt_prot x86_leaf_pt_prot(pt_prot prot) {
    if (prot.value & X86_PT_SMALL_PAT) {
        prot.value &= ~X86_PT_SMALL_PAT;
        prot.value |= X86_PT_HUGE_PAT;
    }

    return prot;
}

static inline void pt3_make_leaf(
    struct pt3 *parent, phys_addr_t phys_addr, pt_prot prot
)
{
    prot = x86_leaf_pt_prot(prot);
    parent->value = phys_addr | prot.value | X86_PT_HUGE;
}

static inline void *pt3_to_virt(struct pt3 *pt)
{
    phys_addr_t phys_addr;
    phys_addr_t mask = X86_PAGE_MASK;

    if (pt->value & X86_PT_HUGE)
        mask = X86_PT3_MASK;

    phys_addr = pt->value & mask;
    return phys_to_virt(phys_addr);
}

MAKE_X86_PT_HELPERS(2)

#define ARCH_HAS_CUSTOM_PT2_LEAF
MAKE_X86_PT_LEAF_HELPERS(2, true) // 2 MiB pages are always available

static inline void pt2_make_leaf(
    struct pt2 *parent, phys_addr_t phys_addr, pt_prot prot
)
{
    prot = x86_leaf_pt_prot(prot);
    parent->value = phys_addr | prot.value | X86_PT_HUGE;
}

static inline void *pt2_to_virt(struct pt2 *pt)
{
    phys_addr_t phys_addr;
    phys_addr_t mask = X86_PAGE_MASK;

    if (pt->value & X86_PT_HUGE)
        mask = X86_PT2_MASK;

    phys_addr = pt->value & mask;
    return phys_to_virt(phys_addr);
}

MAKE_X86_PT_HELPERS(1)
MAKE_X86_PT_TO_VIRT(1)

static inline struct pt1 pt1_make_writeable(struct pt1 pt)
{
    pt.value |= X86_PT_WRITE;
    return pt;
}

#define ARCH_HAS_CUSTOM_PT4_FROM_PT5
struct pt4 *pt4_from_pt5(struct pt5 *pt5, virt_addr_t addr);
