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

typedef phys_addr_t pt_entry_word;

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

#define MAKE_X86_PT_ENTRY_TO_VIRT(idx)                                 \
                                                                       \
    static inline void *pt##idx##_entry_to_virt(pt_entry_word entry)   \
    {                                                                  \
        phys_addr_t phys_addr;                                         \
                                                                       \
        phys_addr = entry & X86_PAGE_MASK;                             \
        return phys_to_virt(phys_addr);                                \
    }

#define MAKE_X86_PT_ENTRY_HELPERS(idx)                                 \
    static inline bool pt##idx##_entry_present(pt_entry_word entry)    \
    {                                                                  \
        return entry & X86_PT_PRESENT;                                 \
    }                                                                  \
                                                                       \
    static inline bool pt##idx##_entry_none(pt_entry_word entry)       \
    {                                                                  \
        return (entry & ~X86_KNL4_ERRATUM_MASK) == 0;                  \
    }

#define MAKE_X86_PT_ENTRY_IS_TABLE(idx)                                \
    static inline bool pt##idx##_entry_is_table(pt_entry_word entry)   \
    {                                                                  \
        return (entry & (X86_PT_PRESENT | X86_PT_HUGE)) ==             \
               X86_PT_PRESENT;                                         \
    }

// Mask that is used for all intermediate page-table levels
#define X86_PT_MASK (X86_PT_PRESENT | X86_PT_WRITE | X86_PT_USER | \
                     X86_PT_ACCESSED)

#define MAKE_X86_PT_TABLE_ENTRY(idx, idx_minus_one)                    \
    static inline pt_entry_word pt##idx##_make_table_entry(            \
        phys_addr_t child                                              \
    )                                                                  \
    {                                                                  \
        return child | X86_PT_MASK;                                    \
    }

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

static inline pt_prot x86_leaf_pt_prot(pt_prot prot) {
    if (prot.value & X86_PT_SMALL_PAT) {
        prot.value &= ~X86_PT_SMALL_PAT;
        prot.value |= X86_PT_HUGE_PAT;
    }

    return prot;
}

/*
 * A leaf-capable level decodes to virt through its own huge-page mask,
 * since a leaf entry there covers the whole range below it.
 */
#define MAKE_X86_PT_LEAF_HELPERS(idx, can_be_leaf_ret)                 \
    static inline bool pt##idx##_can_be_leaf(void)                     \
    {                                                                  \
        return can_be_leaf_ret;                                        \
    }                                                                  \
                                                                       \
    static inline bool pt##idx##_entry_is_leaf(pt_entry_word entry)    \
    {                                                                  \
        return entry & X86_PT_HUGE;                                    \
    }                                                                  \
                                                                       \
    static inline pt_entry_word pt##idx##_make_leaf_entry(             \
        phys_addr_t phys_addr, pt_prot prot)                           \
    {                                                                  \
        prot = x86_leaf_pt_prot(prot);                                 \
        return phys_addr | prot.value | X86_PT_HUGE;                   \
    }                                                                  \
                                                                       \
    static inline void *pt##idx##_entry_to_virt(pt_entry_word entry)   \
    {                                                                  \
        phys_addr_t phys_addr, mask = X86_PAGE_MASK;                   \
                                                                       \
        if (entry & X86_PT_HUGE)                                       \
            mask = X86_PT##idx##_MASK;                                 \
                                                                       \
        phys_addr = entry & mask;                                      \
        return phys_to_virt(phys_addr);                                \
    }

MAKE_X86_PT_ENTRY_HELPERS(5)
MAKE_X86_PT_ENTRY_IS_TABLE(5)
MAKE_X86_PT_ENTRY_TO_VIRT(5)
MAKE_X86_PT_TABLE_ENTRY(5, 4)

MAKE_X86_PT_ENTRY_HELPERS(4)
MAKE_X86_PT_ENTRY_IS_TABLE(4)
MAKE_X86_PT_ENTRY_TO_VIRT(4)
MAKE_X86_PT_TABLE_ENTRY(4, 3)

MAKE_X86_PT_ENTRY_HELPERS(3)
MAKE_X86_PT_ENTRY_IS_TABLE(3)
MAKE_X86_PT_TABLE_ENTRY(3, 2)

extern bool g_have_gb_pages;

#define ARCH_IMPLEMENTS_PT3_LEAF
MAKE_X86_PT_LEAF_HELPERS(3, g_have_gb_pages)

MAKE_X86_PT_ENTRY_HELPERS(2)
MAKE_X86_PT_ENTRY_IS_TABLE(2)
MAKE_X86_PT_TABLE_ENTRY(2, 1)

#define ARCH_IMPLEMENTS_PT2_LEAF
MAKE_X86_PT_LEAF_HELPERS(2, true) // 2 MiB pages are always available

MAKE_X86_PT_ENTRY_HELPERS(1)

static inline pt_entry_word pt1_make_leaf_entry(
    phys_addr_t phys_addr, pt_prot prot
)
{
    return phys_addr | prot.value;
}

#define ARCH_HAS_CUSTOM_PT4_FROM_PT5
