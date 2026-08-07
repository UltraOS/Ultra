#pragma once

#include <common/types.h>
#include <common/error.h>

#include <arch/constants.h>
#include <memory/io.h>

/*
 * Usermode test page-table backend.
 *
 * The generic <memory/page_table.h> drives the page-table walk. This file is
 * the "arch" it walks against. Entries are real u64 words holding "phys | flags"
 * exactly like a hardware page table, except the phys addresses index the
 * harness physical backing store (see test_harness.cpp). PTE pages come from the
 * buddy allocator, so valloc builds genuine multi-level page-table trees that a
 * test can walk to verify every mapping.
 *
 * Five real levels, none folded, 9 index bits each, i.e. a plain 57-bit
 * canonical scheme. That is the most demanding shape for the walk (no level is
 * elided), so it exercises every populate/descend path in valloc.
 *
 * Like a real arch backend, this file only encodes and decodes entry
 * values: all access to page-table memory belongs to the generic layer.
 */

// Raw arch-specific page-table entry flags
typedef struct pt_prot { u64 value; } pt_prot;

// Every page-table entry, at every level, is a single 64-bit word
typedef u64 pt_entry_word;

#define UM_PT_PRESENT (1ull << 0)
#define UM_PT_WRITE   (1ull << 1)
#define UM_PT_NX      (1ull << 2)
#define UM_PT_USER    (1ull << 3)

// Flags live in the low 12 bits, everything above is the (page-aligned) phys
#define UM_PT_FLAG_MASK 0xFFFull
#define UM_PT_PAGE_MASK (~UM_PT_FLAG_MASK)

#define UM_PT_LVL_SHIFT 9
#define UM_PT_LVL_ENTRIES (1 << UM_PT_LVL_SHIFT)

#define PT1_SHIFT PAGE_SHIFT
#define PT1_NUM_ENTRIES UM_PT_LVL_ENTRIES

#define PT2_SHIFT (PT1_SHIFT + UM_PT_LVL_SHIFT)
#define PT2_NUM_ENTRIES UM_PT_LVL_ENTRIES

#define PT3_SHIFT (PT2_SHIFT + UM_PT_LVL_SHIFT)
#define PT3_NUM_ENTRIES UM_PT_LVL_ENTRIES

#define PT4_SHIFT (PT3_SHIFT + UM_PT_LVL_SHIFT)
#define PT4_NUM_ENTRIES UM_PT_LVL_ENTRIES

#define PT5_SHIFT (PT4_SHIFT + UM_PT_LVL_SHIFT)
#define PT5_NUM_ENTRIES UM_PT_LVL_ENTRIES

#define MAKE_UM_PT_ENTRY_HELPERS(idx)                                 \
    static inline bool pt##idx##_entry_present(pt_entry_word entry)   \
    {                                                                 \
        return entry & UM_PT_PRESENT;                                 \
    }                                                                 \
                                                                      \
    static inline bool pt##idx##_entry_none(pt_entry_word entry)      \
    {                                                                 \
        return entry == 0;                                            \
    }                                                                 \
                                                                      \
    static inline bool pt##idx##_entry_is_table(pt_entry_word entry)  \
    {                                                                 \
        return entry & UM_PT_PRESENT;                                 \
    }

#define MAKE_UM_PT_ENTRY_TO_VIRT(idx)                                 \
    static inline void *pt##idx##_entry_to_virt(pt_entry_word entry)  \
    {                                                                 \
        return phys_to_virt(entry & UM_PT_PAGE_MASK);                 \
    }

#define MAKE_UM_PT_TABLE_ENTRY(idx, child_idx)                        \
    static inline pt_entry_word pt##idx##_make_table_entry(           \
        phys_addr_t child                                             \
    )                                                                 \
    {                                                                 \
        return child | UM_PT_PRESENT | UM_PT_WRITE;                   \
    }

MAKE_UM_PT_ENTRY_HELPERS(1)

static inline pt_entry_word pt1_make_leaf_entry(
    phys_addr_t phys, pt_prot prot
)
{
    return phys | prot.value;
}

MAKE_UM_PT_ENTRY_HELPERS(2)
MAKE_UM_PT_ENTRY_TO_VIRT(2)
MAKE_UM_PT_TABLE_ENTRY(2, 1)

MAKE_UM_PT_ENTRY_HELPERS(3)
MAKE_UM_PT_ENTRY_TO_VIRT(3)
MAKE_UM_PT_TABLE_ENTRY(3, 2)

MAKE_UM_PT_ENTRY_HELPERS(4)
MAKE_UM_PT_ENTRY_TO_VIRT(4)
MAKE_UM_PT_TABLE_ENTRY(4, 3)

MAKE_UM_PT_ENTRY_HELPERS(5)
MAKE_UM_PT_ENTRY_TO_VIRT(5)
MAKE_UM_PT_TABLE_ENTRY(5, 4)
