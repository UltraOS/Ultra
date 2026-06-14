#pragma once

#include <arch/page_table.h>
#include <common/helpers.h>
#include <memory/vm_flags.h>

// Number of bytes of the address space covered by the respective pt level
#define PT1_SIZE (1ul << PT1_SHIFT)
#define PT2_SIZE (1ul << PT2_SHIFT)
#define PT3_SIZE (1ul << PT3_SHIFT)
#define PT4_SIZE (1ul << PT4_SHIFT)
#define PT5_SIZE (1ul << PT5_SHIFT)

#define MAKE_GENERIC_PTX_IS_FOLDED(lvl, value) \
    static inline bool pt##lvl##_is_folded(void) { return value; }

#ifndef ARCH_HAS_CUSTOM_PT5_IS_FOLDED
    MAKE_GENERIC_PTX_IS_FOLDED(5, false)
#endif

#ifndef ARCH_HAS_CUSTOM_PT4_IS_FOLDED
    MAKE_GENERIC_PTX_IS_FOLDED(4, false)
#endif

/*
 * The minimum number of real page table levels that we support is 2,
 * which should hold for every 64-bit architecture and page size
 * combination.
 */
#ifndef ARCH_HAS_CUSTOM_PT3_IS_FOLDED
    MAKE_GENERIC_PTX_IS_FOLDED(3, false)
#endif

#define DO_MAKE_GENERIC_PTN_INDEX(lvl, shift, num_entries) \
    static inline size_t pt##lvl##_index(virt_addr_t addr) \
    {                                                      \
        return (addr >> (shift)) & ((num_entries) - 1);    \
    }

#define MAKE_GENERIC_PTN_INDEX(lvl)           \
    DO_MAKE_GENERIC_PTN_INDEX(                \
        lvl,                                  \
        CONCAT(CONCAT(PT, lvl), _SHIFT),      \
        CONCAT(CONCAT(PT, lvl), _NUM_ENTRIES) \
    )

#define MAKE_ENOSYS_PTN_LEAF(lvl)                                  \
    static inline bool pt##lvl##_can_be_leaf(void)                 \
    {                                                              \
        return false;                                              \
    }                                                              \
                                                                   \
    static inline bool pt##lvl##_is_leaf(void)                     \
    {                                                              \
        return false;                                              \
    }                                                              \
                                                                   \
    static inline error_t pt##lvl##_make_leaf(                     \
        struct pt##lvl *pt, phys_addr_t addr, pt_prot prot)        \
    {                                                              \
        UNREFERENCED_PARAMETER(pt);                                \
        UNREFERENCED_PARAMETER(addr);                              \
        UNREFERENCED_PARAMETER(prot);                              \
        return ENOSYS;                                             \
    }

#ifndef ARCH_HAS_CUSTOM_PT5_INDEX
MAKE_GENERIC_PTN_INDEX(5)
#endif

#ifndef ARCH_HAS_CUSTOM_PT5_LEAF
MAKE_ENOSYS_PTN_LEAF(5)
#endif

#ifndef ARCH_HAS_CUSTOM_PT4_INDEX
MAKE_GENERIC_PTN_INDEX(4)
#endif

#ifndef ARCH_HAS_CUSTOM_PT4_LEAF
MAKE_ENOSYS_PTN_LEAF(4)
#endif

#ifndef ARCH_HAS_CUSTOM_PT3_INDEX
MAKE_GENERIC_PTN_INDEX(3)
#endif

#ifndef ARCH_HAS_CUSTOM_PT3_LEAF
MAKE_ENOSYS_PTN_LEAF(3)
#endif

#ifndef ARCH_HAS_CUSTOM_PT2_INDEX
MAKE_GENERIC_PTN_INDEX(2)
#endif

#ifndef ARCH_HAS_CUSTOM_PT2_LEAF
MAKE_ENOSYS_PTN_LEAF(2)
#endif

#ifndef ARCH_HAS_CUSTOM_PT1_INDEX
MAKE_GENERIC_PTN_INDEX(1)
#endif

#define MAKE_GENERIC_PTN_FROM_PTN(target, current)                  \
    static inline struct pt##target *pt##target##_from_pt##current( \
        struct pt##current *pt##current, virt_addr_t addr           \
    )                                                               \
    {                                                               \
        struct pt##target *pt##target;                              \
                                                                    \
        pt##target = pt##current##_to_virt(pt##current);            \
        return &pt##target[pt##target##_index(addr)];               \
    }

#ifndef ARCH_HAS_CUSTOM_PT5_FROM_PT5_BASE
static inline struct pt5 *pt5_from_pt5_base(struct pt5 *pt5, virt_addr_t addr)
{
    return &pt5[pt5_index(addr)];
}
#endif

#ifndef ARCH_HAS_CUSTOM_PT4_FROM_PT5
MAKE_GENERIC_PTN_FROM_PTN(4, 5)
#endif

#ifndef ARCH_HAS_CUSTOM_PT3_FROM_PT4
MAKE_GENERIC_PTN_FROM_PTN(3, 4)
#endif

#ifndef ARCH_HAS_CUSTOM_PT2_FROM_PT3
MAKE_GENERIC_PTN_FROM_PTN(2, 3)
#endif

#ifndef ARCH_HAS_CUSTOM_PT1_FROM_PT2
MAKE_GENERIC_PTN_FROM_PTN(1, 2)
#endif

pt_prot pt_prot_from_vm_prot(enum vm_prot);

typedef struct pt5 pt_root;
#define pt_root_from_address_space(as, va) pt5_from_pt5_base((as)->pt, va);
