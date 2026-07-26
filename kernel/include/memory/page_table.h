#pragma once

#include <arch/page_table.h>

#include <common/atomic.h>
#include <common/helpers.h>
#include <memory/bug.h>
#include <memory/vm_flags.h>

/*
 * Page table access
 *
 * This header owns every read and write of a live page table entry
 * in the kernel. Callers walk and mutate page tables exclusively
 * through the ptN helpers below.
 *
 * Levels are numbered from the bottom up. pt1 holds the smallest
 * leaves and pt5 is the root. Levels the hardware does not implement
 * are folded, see "Folded levels".
 *
 * Walking
 * =======
 *
 *   ptN_index(addr)              entry index for an address
 *   ptN-1_from_ptN(parent, addr) descend one level
 *   ptN_present, ptN_none        state of an entry
 *   ptN_is_leaf, ptN_can_be_leaf
 *   ptN_to_virt                  the table an entry points to
 *
 * Writing an entry
 * ================
 *
 * Three groups, one per kind of exclusion. Each name says what
 * exclusion its caller must already hold.
 *
 *   ptN_cmpxchg_populate       Lockless install of a table into an
 *                              empty entry. Returns false if another
 *                              CPU installed one first. Kernel address
 *                              space only, see "Invariants".
 *
 *   ptN_exclusive_populate     Unconditional write into an entry the
 *   ptN_exclusive_make_leaf    caller already owns, either because the
 *                              table is not reachable by the MMU yet,
 *                              or because the virtual range it covers
 *                              is exclusively reserved by the caller.
 *                              Examples are: early init, valloc leaf
 *                              writes, offline page table construction.
 *
 *                              Both assert the target reads as a
 *                              literal zero, not merely as none. The
 *                              target is either a freshly zeroed table
 *                              or one this caller cleared itself, so
 *                              any other value means the caller does
 *                              not own the entry it claims to. Note
 *                              this makes exclusive_make_leaf an
 *                              install, not an update: changing the
 *                              protections of a live leaf in place
 *                              would need a separate helper.
 *
 *   ptN_exclusive_clear        Same exclusion as above. Clearing
 *                              publishes nothing, so it is a relaxed
 *                              store. Ordering against reuse of the
 *                              memory comes from the caller's TLB
 *                              invalidation.
 *
 *   ptN_get_and_clear          Atomically exchanges a leaf entry for
 *                              zero and returns the old word, decoded
 *                              with the ptN_entry helpers. Same software
 *                              exclusion as the ptN_exclusive_* family.
 *                              This helper is used when the caller needs
 *                              to be able to inspect the final PTE state
 *                              after clearing it, such as the A/D bits set
 *                              by the MMU. Otherwise, a plain
 *                              ptN_exclusive_clear should be used.
 *
 * Teardown
 * ========
 *
 * Removal is asymmetric to installation on purpose. Installs race
 * legally, so ptN_cmpxchg_populate returns bool. Removal of a given
 * entry has a single owner: the path that detached the covering
 * virtual range from its lookup structure first (a released
 * reservation, an unmapped region). Nothing else may touch the entry
 * after that, so clears are unconditional stores and return nothing.
 *
 * A table may be freed only once no live range intersects its span,
 * at which point it is empty by construction, since every entry
 * belonged to some range that has been*  torn down.
 *
 * Invariants
 * ==========
 *
 * Kernel address space:
 *
 *   Intermediate entries are installed once and never cleared, so an
 *   entry is either an untouched zero or a valid table for the
 *   lifetime of the system. Nothing ever writes to an entry a walk
 *   has not descended through, so no hardware bit can appear in one
 *   that was never present, which is what lets ptN_cmpxchg_populate
 *   expect a literal zero rather than asking ptN_entry_none. Any other
 *   observed value is corruption rather than a state to handle, and the
 *   helper asserts on it.
 *
 *   This holds only as long as neither clear helper is called on a
 *   kernel table above pt1.
 *
 *   Leaf entries are written and cleared with no lock at all. The
 *   exclusion comes from the virtual range being reserved by its
 *   owner before it is mapped and released only after it is unmapped.
 *
 *   The accessed and dirty bits are preset in kernel protections, so
 *   the hardware never writes back into a live kernel leaf, and a
 *   cleared one stays a literal zero.
 *
 * Folded levels
 * =============
 *
 * A folded level has no entries of its own. A walk descends straight
 * through it into the level below, so its entry always reads as
 * present and is never empty. That is what keeps folded levels off
 * the install path: a walker allocates and populates only where
 * ptN_none says the entry is empty, which a folded level never does.
 * The bottom two levels are always real. Reaching a folded level in
 * a helper that installs or clears an entry means the walker skipped
 * that check, so those helpers assert rather than silently
 * absorbing the write.
 *
 * Descending into a folded level needs ARCH_HAS_CUSTOM_PTN_FROM_PTN.
 * The generic ptN_from_ptN calls to_virt on the parent, but a folded
 * child has no table of its own and the parent's entry must be
 * passed through unchanged so the next real level decodes it. Note
 * the override is on the descent into the folded level, not out of
 * it.
 *
 * Porting to other architectures
 * ==============================
 *
 * <arch/page_table.h> is the only mandatory header, it is expected to
 * provide:
 *
 *   pt_entry_word              one machine word, loadable and
 *                              storable atomically by the CPU
 *
 *   pt_prot                    opaque arch entry flags
 *   PTN_SHIFT, PTN_NUM_ENTRIES shape of each level
 *
 *   ptN_entry_present          decoders, each taking a pt_entry_word
 *   ptN_entry_none
 *   ptN_entry_is_table
 *   ptN_entry_to_virt
 *
 *   ptN_make_table_entry       encoders, taking either a child table
 *                              physical address...
 *   pt1_make_leaf_entry        ...or a direct physical address and
 *                              protections
 *
 * The encoders and decoders listed above take and return pt_entry_word
 * values only, never a struct ptN pointer, which is what makes a plain
 * load or store unrepresentable outside this file.
 *
 * <arch/page_table_extras.h> is included (if provided) after the
 * generic definitions and holds anything that needs a struct ptN
 * pointer or a generic helper defined before it. The matching guard
 * still belongs in <arch/page_table.h>, since the guards are tested
 * before this header is read.
 *
 * ptN_entry_none decides whether an entry is empty. It must ignore
 * any bit the hardware is able to set in a non present entry, so
 * emptiness stays a decode question rather than a comparison against
 * zero.
 *
 * pt1 is always a leaf level. Any level above it that is able to hold
 * leaves defines ARCH_IMPLEMENTS_PTN_LEAF and provides
 * ptN_can_be_leaf, ptN_entry_is_leaf and ptN_make_leaf_entry. The
 * guard is a build time statement that the architecture encodes
 * leaves at that level, while ptN_can_be_leaf answers at runtime
 * whether this particular CPU has them. Levels without the guard get
 * stubs that assert if a leaf is ever written through them.
 *
 * Unlike the guard above, each generic definition below already has a
 * working implementation and can merely be replaced by defining the
 * matching guard in the arch header and providing the function there:
 *
 *   ARCH_HAS_CUSTOM_PTN_IS_FOLDED
 *   ARCH_HAS_CUSTOM_PTN_INDEX
 *   ARCH_HAS_CUSTOM_PT5_FROM_PT5_BASE
 *
 * pt_prot_from_vm_prot is declared here and defined by the arch.
 */

#define MAKE_PT_TYPE(lvl) struct pt##lvl { pt_entry_word value; };

MAKE_PT_TYPE(1)
MAKE_PT_TYPE(2)
MAKE_PT_TYPE(3)
MAKE_PT_TYPE(4)
MAKE_PT_TYPE(5)

// Number of bytes of the address space covered by the respective pt level
#define PT1_SIZE (1ul << PT1_SHIFT)
#define PT2_SIZE (1ul << PT2_SHIFT)
#define PT3_SIZE (1ul << PT3_SHIFT)
#define PT4_SIZE (1ul << PT4_SHIFT)
#define PT5_SIZE (1ul << PT5_SHIFT)

/*
 * Entry cells
 *
 * Internal to this header. The ptN helpers above are built on these
 * and client code has no reason to call them directly.
 *
 * An entry is one pt_entry_word that the MMU and other CPUs may
 * observe at any time, so every access to a live one goes through a
 * cell helper. These are atomic only in the C11 sense: one
 * indivisible access, no tearing, and no splitting, inventing,
 * fusing or reordering by the compiler.
 *
 *   pt_entry_read              relaxed load, value is inspected only
 *   pt_entry_read_table        acquire load, the caller goes on to
 *                              dereference the table it points to
 *   pt_entry_write             relaxed store by an exclusive owner
 *   pt_entry_publish           release store of a prepared table
 *   pt_entry_cmpxchg_populate  release compare exchange against an
 *                              empty entry, returns the observed word
 *   pt_entry_get_and_clear     relaxed exchange for zero, returns the
 *                              old word with any hardware bits the
 *                              MMU managed to set before the swap
 *
 * The acquire in pt_entry_read_table pairs with the release in
 * pt_entry_publish and pt_entry_cmpxchg_populate. That pairing is
 * what guarantees a walker sees a table's zeroed entries before it
 * can see the pointer to them.
 * C has no usable dependency ordering, since consume is deprecated
 * to acquire, so the load is spelled acquire even though the
 * hardware would order it through the address dependency alone.
 */
static inline pt_entry_word pt_entry_read(const pt_entry_word *entry)
{
    return atomic_load_relaxed(entry);
}

static inline pt_entry_word pt_entry_read_table(const pt_entry_word *entry)
{
    return atomic_load_acquire(entry);
}

static inline void pt_entry_write(pt_entry_word *entry, pt_entry_word value)
{
    atomic_store_relaxed(entry, value);
}

static inline void pt_entry_publish(pt_entry_word *entry, pt_entry_word value)
{
    atomic_store_release(entry, value);
}

/*
 * Publish 'value' into an entry that is expected to be non-present.
 * Returns the value observed by the compare-exchange: zero if this
 * caller won and 'value' is now installed, otherwise whatever the
 * winner left there, which the caller decodes.
 */
static inline pt_entry_word pt_entry_cmpxchg_populate(
    pt_entry_word *entry, pt_entry_word value
)
{
    pt_entry_word expected = 0;
    bool success;

    success = atomic_cmpxchg_explicit(
        entry, &expected, value, MO_RELEASE, MO_RELAXED
    );
    if (success)
        return 0;

    return expected;
}

/*
 * Clearing publishes nothing, so the exchange is relaxed like
 * pt_entry_write.
 */
static inline pt_entry_word pt_entry_get_and_clear(pt_entry_word *entry)
{
    return atomic_xchg(entry, 0, MO_RELAXED);
}

/*
 * Per level emitters
 *
 * Every macro below is undefined at the end of this header, so an
 * arch extras header may use them but nothing else can.
 */

#define MAKE_GENERIC_PTX_IS_FOLDED(lvl, value) \
    static inline bool pt##lvl##_is_folded(void) { return value; }

/*
 * A folded level has no entries of its own: the walk descends straight
 * through it into the level below, so its entry always reads as present
 * and an attempt to populate it is a bug in the page table walker.
 */
#define MAKE_GENERIC_PTN_ACCESSORS(lvl)                                \
    static inline bool pt##lvl##_present(struct pt##lvl *pt)           \
    {                                                                  \
        if (pt##lvl##_is_folded())                                     \
            return true;                                               \
                                                                       \
        return pt##lvl##_entry_present(pt_entry_read(&pt->value));     \
    }                                                                  \
                                                                       \
    static inline bool pt##lvl##_none(struct pt##lvl *pt)              \
    {                                                                  \
        if (pt##lvl##_is_folded())                                     \
            return false;                                              \
                                                                       \
        return pt##lvl##_entry_none(pt_entry_read(&pt->value));        \
    }                                                                  \
                                                                       \
    static inline void pt##lvl##_exclusive_clear(struct pt##lvl *pt)   \
    {                                                                  \
        MM_BUG_ON(pt##lvl##_is_folded());                              \
        pt_entry_write(&pt->value, 0);                                 \
    }

#define MAKE_GENERIC_PTN_TO_VIRT(lvl)                                  \
    static inline void *pt##lvl##_to_virt(struct pt##lvl *pt)          \
    {                                                                  \
        pt_entry_word entry;                                           \
                                                                       \
        entry = pt_entry_read_table(&pt->value);                       \
        return pt##lvl##_entry_to_virt(entry);                         \
    }

#define MAKE_GENERIC_PTN_POPULATE(lvl, child_lvl)                      \
    static inline void pt##lvl##_exclusive_populate(                   \
        struct pt##lvl *parent, struct pt##child_lvl *child            \
    )                                                                  \
    {                                                                  \
        /* A folded level reads as present, so nothing to install */   \
        MM_BUG_ON(pt##lvl##_is_folded());                              \
        MM_BUG_ON(pt_entry_read(&parent->value) != 0);                 \
                                                                       \
        pt_entry_write(                                                \
            &parent->value,                                            \
            pt##lvl##_make_table_entry(virt_to_phys(child))            \
        );                                                             \
    }                                                                  \
                                                                       \
    static inline bool pt##lvl##_cmpxchg_populate(                     \
        struct pt##lvl *parent, struct pt##child_lvl *child            \
    )                                                                  \
    {                                                                  \
        pt_entry_word observed;                                        \
                                                                       \
        /* A folded level reads as present, so nothing to install */   \
        MM_BUG_ON(pt##lvl##_is_folded());                              \
                                                                       \
        observed = pt_entry_cmpxchg_populate(                          \
            &parent->value,                                            \
            pt##lvl##_make_table_entry(virt_to_phys(child))            \
        );                                                             \
        if (observed == 0)                                             \
            return true;                                               \
                                                                       \
        /* Anything but a table here means corrupted page tables */    \
        MM_BUG_ON(!pt##lvl##_entry_is_table(observed));                \
        return false;                                                  \
    }

#define MAKE_GENERIC_PTN_IS_LEAF(lvl)                                  \
    static inline bool pt##lvl##_is_leaf(struct pt##lvl *pt)           \
    {                                                                  \
        return pt##lvl##_entry_is_leaf(pt_entry_read(&pt->value));     \
    }

#define MAKE_GENERIC_PTN_LEAF_ACCESSORS(lvl)                           \
    static inline void pt##lvl##_exclusive_make_leaf(                  \
        struct pt##lvl *pt, phys_addr_t phys_addr, pt_prot prot        \
    )                                                                  \
    {                                                                  \
        /* A folded level reads as present, so nothing to install */   \
        MM_BUG_ON(pt##lvl##_is_folded());                              \
        MM_BUG_ON(pt_entry_read(&pt->value) != 0);                     \
                                                                       \
        pt_entry_write(                                                \
            &pt->value, pt##lvl##_make_leaf_entry(phys_addr, prot)     \
        );                                                             \
    }                                                                  \
                                                                       \
    static inline pt_entry_word pt##lvl##_get_and_clear(               \
        struct pt##lvl *pt                                             \
    )                                                                  \
    {                                                                  \
        MM_BUG_ON(pt##lvl##_is_folded());                              \
        return pt_entry_get_and_clear(&pt->value);                     \
    }

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

#define MAKE_UNSUPPORTED_PTN_LEAF(lvl)                       \
    static inline bool pt##lvl##_can_be_leaf(void)           \
    {                                                        \
        return false;                                        \
    }                                                        \
                                                             \
    static inline bool pt##lvl##_is_leaf(struct pt##lvl *pt) \
    {                                                        \
        UNREFERENCED_PARAMETER(pt);                          \
        return false;                                        \
    }                                                        \
                                                             \
    static inline void pt##lvl##_exclusive_make_leaf(        \
        struct pt##lvl *pt, phys_addr_t addr, pt_prot prot   \
    )                                                        \
    {                                                        \
        UNREFERENCED_PARAMETER(pt);                          \
        UNREFERENCED_PARAMETER(addr);                        \
        UNREFERENCED_PARAMETER(prot);                        \
                                                             \
        MM_BUG_ON(true);                                     \
    }                                                        \
                                                             \
    static inline pt_entry_word pt##lvl##_get_and_clear(     \
        struct pt##lvl *pt                                   \
    )                                                        \
    {                                                        \
        UNREFERENCED_PARAMETER(pt);                          \
                                                             \
        MM_BUG_ON(true);                                     \
        return 0;                                            \
    }

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

MAKE_GENERIC_PTX_IS_FOLDED(2, false)
MAKE_GENERIC_PTX_IS_FOLDED(1, false)

#ifndef ARCH_HAS_CUSTOM_PT5_INDEX
MAKE_GENERIC_PTN_INDEX(5)
#endif

MAKE_GENERIC_PTN_ACCESSORS(5)
MAKE_GENERIC_PTN_TO_VIRT(5)
MAKE_GENERIC_PTN_POPULATE(5, 4)

#ifdef ARCH_IMPLEMENTS_PT5_LEAF
MAKE_GENERIC_PTN_IS_LEAF(5)
MAKE_GENERIC_PTN_LEAF_ACCESSORS(5)
#else
MAKE_UNSUPPORTED_PTN_LEAF(5)
#endif

#ifndef ARCH_HAS_CUSTOM_PT4_INDEX
MAKE_GENERIC_PTN_INDEX(4)
#endif

MAKE_GENERIC_PTN_ACCESSORS(4)
MAKE_GENERIC_PTN_TO_VIRT(4)
MAKE_GENERIC_PTN_POPULATE(4, 3)

#ifdef ARCH_IMPLEMENTS_PT4_LEAF
MAKE_GENERIC_PTN_IS_LEAF(4)
MAKE_GENERIC_PTN_LEAF_ACCESSORS(4)
#else
MAKE_UNSUPPORTED_PTN_LEAF(4)
#endif

#ifndef ARCH_HAS_CUSTOM_PT3_INDEX
MAKE_GENERIC_PTN_INDEX(3)
#endif

MAKE_GENERIC_PTN_ACCESSORS(3)
MAKE_GENERIC_PTN_TO_VIRT(3)
MAKE_GENERIC_PTN_POPULATE(3, 2)

#ifdef ARCH_IMPLEMENTS_PT3_LEAF
MAKE_GENERIC_PTN_IS_LEAF(3)
MAKE_GENERIC_PTN_LEAF_ACCESSORS(3)
#else
MAKE_UNSUPPORTED_PTN_LEAF(3)
#endif

#ifndef ARCH_HAS_CUSTOM_PT2_INDEX
MAKE_GENERIC_PTN_INDEX(2)
#endif

MAKE_GENERIC_PTN_ACCESSORS(2)
MAKE_GENERIC_PTN_TO_VIRT(2)
MAKE_GENERIC_PTN_POPULATE(2, 1)

#ifdef ARCH_IMPLEMENTS_PT2_LEAF
MAKE_GENERIC_PTN_IS_LEAF(2)
MAKE_GENERIC_PTN_LEAF_ACCESSORS(2)
#else
MAKE_UNSUPPORTED_PTN_LEAF(2)
#endif

#ifndef ARCH_HAS_CUSTOM_PT1_INDEX
MAKE_GENERIC_PTN_INDEX(1)
#endif

MAKE_GENERIC_PTN_ACCESSORS(1)

// Every pt1 entry is a leaf, there is no level below it to point at
static inline bool pt1_can_be_leaf(void)
{
    return true;
}

static inline bool pt1_is_leaf(struct pt1 *pt)
{
    UNREFERENCED_PARAMETER(pt);
    return true;
}

MAKE_GENERIC_PTN_LEAF_ACCESSORS(1)

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
#define pt_root_from_address_space(as, va) pt5_from_pt5_base((as)->pt, va)

#if HAS_INCLUDE(<arch/page_table_extras.h>)
#include <arch/page_table_extras.h>
#endif

#undef MAKE_PT_TYPE
#undef MAKE_GENERIC_PTX_IS_FOLDED
#undef MAKE_GENERIC_PTN_ACCESSORS
#undef MAKE_GENERIC_PTN_TO_VIRT
#undef MAKE_GENERIC_PTN_POPULATE
#undef MAKE_GENERIC_PTN_IS_LEAF
#undef MAKE_GENERIC_PTN_LEAF_ACCESSORS
#undef DO_MAKE_GENERIC_PTN_INDEX
#undef MAKE_GENERIC_PTN_INDEX
#undef MAKE_UNSUPPORTED_PTN_LEAF
#undef MAKE_GENERIC_PTN_FROM_PTN
