#include <arch/page_table.h>
#include <common/helpers.h>

#include <memory/address_space.h>

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

#ifndef ARCH_HAS_CUSTOM_PT5_INDEX
MAKE_GENERIC_PTN_INDEX(5)
#endif

#ifndef ARCH_HAS_CUSTOM_PT4_INDEX
MAKE_GENERIC_PTN_INDEX(4)
#endif

#ifndef ARCH_HAS_CUSTOM_PT3_INDEX
MAKE_GENERIC_PTN_INDEX(3)
#endif

#ifndef ARCH_HAS_CUSTOM_PT2_INDEX
MAKE_GENERIC_PTN_INDEX(2)
#endif

#ifndef ARCH_HAS_CUSTOM_PT1_INDEX
MAKE_GENERIC_PTN_INDEX(1)
#endif

#define MAKE_GENERIC_PTN_FROM_PTN(target, current)                \
    static inline struct pt##target *pt##target_from_pt##current( \
        struct pt##current *pt##current, virt_addr_t addr         \
    )                                                             \
    {                                                             \
        struct pt##target *pt##target;                            \
                                                                  \
        pt##target = pt##current##_to_virt(pt##current);          \
        return &pt##target[pt##target##_index(addr)];             \
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
