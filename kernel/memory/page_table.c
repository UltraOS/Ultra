#include <memory/page_table.h>
#include <memory/page.h>

#include <spinlock.h>

#define MAKE_GENERIC_PTN_POPULATE_IMPL(lvl, child_lvl)                 \
    bool pt##lvl##_populate(                                           \
        struct pt##lvl *parent, struct pt##child_lvl *child            \
    )                                                                  \
    {                                                                  \
        struct ptdesc_page *desc;                                      \
        bool did_install = false;                                      \
                                                                       \
        /* A folded level reads as present, so nothing to install */   \
        MM_BUG_ON(pt##lvl##_is_folded());                              \
        desc = block_ptdesc(virt_to_block(parent));                    \
                                                                       \
        spin_lock(&desc->lock);                                        \
        if (likely(pt##lvl##_none(parent))) {                          \
            pt_entry_publish(                                          \
                &parent->value,                                        \
                pt##lvl##_make_table_entry(virt_to_phys(child))        \
            );                                                         \
            did_install = true;                                        \
        }                                                              \
        spin_unlock(&desc->lock);                                      \
        return did_install;                                            \
    }

#define MAKE_GENERIC_PTN_MAKE_LEAF_IMPL(lvl)                           \
    bool pt##lvl##_make_leaf(                                          \
        struct pt##lvl *pt, phys_addr_t phys_addr, pt_prot prot        \
    )                                                                  \
    {                                                                  \
        struct ptdesc_page *desc;                                      \
        bool did_install = false;                                      \
                                                                       \
        /* A folded level reads as present, so nothing to install */   \
        MM_BUG_ON(pt##lvl##_is_folded());                              \
        desc = block_ptdesc(virt_to_block(pt));                        \
                                                                       \
        spin_lock(&desc->lock);                                        \
        if (likely(pt##lvl##_none(pt))) {                              \
            pt_entry_publish(                                          \
                &pt->value,                                            \
                pt##lvl##_make_leaf_entry(phys_addr, prot)             \
            );                                                         \
            did_install = true;                                        \
        }                                                              \
        spin_unlock(&desc->lock);                                      \
        return did_install;                                            \
    }

// Populate is unconditionally generic at this moment
MAKE_GENERIC_PTN_POPULATE_IMPL(5, 4)
MAKE_GENERIC_PTN_POPULATE_IMPL(4, 3)
MAKE_GENERIC_PTN_POPULATE_IMPL(3, 2)
MAKE_GENERIC_PTN_POPULATE_IMPL(2, 1)

#ifdef ARCH_IMPLEMENTS_PT5_LEAF
MAKE_GENERIC_PTN_MAKE_LEAF_IMPL(5)
#endif

#ifdef ARCH_IMPLEMENTS_PT4_LEAF
MAKE_GENERIC_PTN_MAKE_LEAF_IMPL(4)
#endif

#ifdef ARCH_IMPLEMENTS_PT3_LEAF
MAKE_GENERIC_PTN_MAKE_LEAF_IMPL(3)
#endif

#ifdef ARCH_IMPLEMENTS_PT2_LEAF
MAKE_GENERIC_PTN_MAKE_LEAF_IMPL(2)
#endif

MAKE_GENERIC_PTN_MAKE_LEAF_IMPL(1)

#undef MAKE_GENERIC_PTN_POPULATE_IMPL
#undef MAKE_GENERIC_PTN_MAKE_LEAF_IMPL
