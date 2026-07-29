#define MSG_FMT(x) "valloc: " x

#include <common/rb_tree_aggregated.h>
#include <common/list.h>
#include <common/align.h>
#include <common/types.h>
#include <common/minmax.h>
#include <common/string.h>
#include <common/bit.h>

#include <bug.h>

#include <memory/alloc.h>
#include <memory/alloc_cache.h>
#include <memory/valloc.h>
#include <memory/page.h>
#include <memory/address_space.h>
#include <memory/buddy.h>
#include <memory/page_table.h>
#include <memory/tlb.h>
#include <memory/units.h>

#include <private/buddy.h>
#include <private/memory.h>

#include <arch/memory.h>

#include <free_after_init.h>
#include <init_level.h>
#include <spinlock.h>
#include <irq_helpers.h>
#include <per_cpu.h>

enum varea_type : u8 {
    // A free hole, lives on the free tree (max_subtree_size is valid)
    VAREA_FREE = 0,
    // An opaque virtual memory reservation (backing memory is not ours)
    VAREA_RESERVATION,
    // The backing memory is owned by us
    VAREA_MANAGED,
};

enum valloc_flags : u8 {
    // This region has been mapped
    VALLOC_MAPPED = BIT_U8(0),
    // A permanent reservation
    VALLOC_PERMANENT = BIT_U8(1),
    // The last page of the reservation is an unmapped guard
    VALLOC_GUARDED = BIT_U8(2),
};

struct vallocation {
    enum valloc_flags flags;

    union {
        void *caller;
        const char *what;
    };

    union {
        // Only applicable for VAREA_MANAGED
        struct page_block **pages;
        // Only applicable for VAREA_RESERVATION
        phys_addr_t phys_addr;
    };
};

struct varea {
    virt_addr_t start;
    virt_addr_t end;

    enum varea_type type;

    struct rb_node node;
    struct list_link link;

    union {
        // For busy areas (reserved tree)
        struct vallocation *info;
        // For free areas (free tree)
        size_t max_subtree_size;
    };
};

static NORETURN void corrupted_varea(
    const struct varea *area, const char *why
)
{
    panic(
        "Corrupted varea %p [0x%016zX - 0x%016zX] (type=%d): %s",
        area, area->start, area->end, area->type, why
    );
}

// A macro so that the constness of the node is preserved by rb_entry
#define varea_of(rb) rb_entry(rb, struct varea, node)

static inline size_t varea_size(const struct varea *area)
{
    return area->end - area->start;
}

// The end of the mappable part of a reservation, guard page excluded
static inline virt_addr_t varea_usable_end(const struct varea *area)
{
    virt_addr_t end = area->end;

    if (area->info->flags & VALLOC_GUARDED)
        end -= PAGE_SIZE;

    return end;
}

static inline size_t varea_usable_size(const struct varea *area)
{
    return varea_usable_end(area) - area->start;
}

static inline size_t varea_pages(const struct varea *area)
{
    return varea_usable_size(area) >> PAGE_SHIFT;
}

static inline void *varea_to_ptr(const struct varea *area)
{
    return (void*)area->start;
}

static inline bool varea_mapped(const struct varea *area)
{
    BUG_ON(!area->info);
    return area->info->flags & VALLOC_MAPPED;
}

static inline bool varea_is_managed(const struct varea *area)
{
    switch (area->type) {
    case VAREA_MANAGED:
        return true;
    case VAREA_RESERVATION:
    case VAREA_FREE:
        return false;
    default:
        corrupted_varea(area, "type");
    }
}

static inline bool varea_is_permanent(const struct varea *area)
{
    return area->info->flags & VALLOC_PERMANENT;
}

AGGREGATED_SUBTREE_MAX_RBTREE_OPS(
    static, free_ranges_tree, struct varea, node,
    max_subtree_size, varea_size
);
static struct rb_root s_free_ranges = RB_ROOT_INIT;
static struct rb_root s_reserved_ranges = RB_ROOT_INIT;

static struct alloc_cache s_varea_cache;
static struct alloc_cache s_vallocation_cache;

// Address-ordered list of every free area, mirroring the free tree
static LIST_HEAD(s_free_areas);

// Protects the rb-trees and the free list
static struct spinlock s_valloc_lock;

static pt_prot s_managed_mappings_prot;

// Tree & list bookkeeping

static bool varea_start_less(const struct rb_node *a, const struct rb_node *b)
{
    return varea_of(a)->start < varea_of(b)->start;
}

static int reserved_key_cmp(const void *key, const struct rb_node *node)
{
    virt_addr_t addr = (virt_addr_t)key;
    const struct varea *area;

    area = varea_of(node);
    if (addr < area->start)
        return -1;
    if (addr >= area->end)
        return 1;

    return 0;
}

static inline size_t free_subtree_max(const struct rb_node *node)
{
    if (node == nullptr)
        return 0;

    return varea_of(node)->max_subtree_size;
}

static void free_tree_insert(struct varea *area)
{
    area->type = VAREA_FREE;
    rb_node_insert_aggregated(
        &area->node, &s_free_ranges, varea_start_less, &free_ranges_tree_ops
    );
}

static void free_tree_remove(struct varea *area)
{
    rb_node_remove_aggregated(
        &area->node, &s_free_ranges, &free_ranges_tree_ops
    );
}

static void reserved_tree_insert(struct varea *area)
{
    rb_node_insert(&area->node, &s_reserved_ranges, varea_start_less);
}

static void reserved_tree_remove(struct varea *area)
{
    rb_node_remove(&area->node, &s_reserved_ranges);
}

static struct varea *reserved_area_find(virt_addr_t addr)
{
    struct rb_node *node;

    node = rb_node_find((void*)addr, &s_reserved_ranges, reserved_key_cmp);
    if (node == nullptr)
        return nullptr;

    return varea_of(node);
}

/*
 * Resolve an address handed back to us by a caller of the public API. Anything
 * other than the exact start of a releasable reservation means the caller is
 * confused about what it owns, so all three failures are fatal.
 */
static struct varea *reserved_area_get(virt_addr_t addr, const char *caller)
{
    struct varea *area;

    area = reserved_area_find(addr);
    if (unlikely(area == nullptr))
        panic("%s: 0x%016zX is not a valloc address", caller, addr);

    if (unlikely(area->start != addr))
        panic(
            "%s: 0x%016zX points into the middle of [0x%016zX - 0x%016zX]",
            caller, addr, area->start, area->end
        );

    if (unlikely(varea_is_permanent(area)))
        panic(
            "attempt to %s() a permanent reservation '%s'",
            caller, area->info->what
        );

    return area;
}

/*
 * Locate the tree position for a free area starting at 'start': the parent
 * node and the link to hook it onto. The address-ordered list position (the
 * link to insert after) falls out of the same walk, so the mirror list and
 * both merge neighbours come for free without a second descent.
 */
static struct list_link *free_tree_find_links(
    virt_addr_t start, struct rb_node **out_parent, struct rb_node ***out_link
)
{
    struct rb_node *parent = nullptr;
    struct rb_node **link = &s_free_ranges.root;
    struct varea *area;

    while (*link) {
        parent = *link;
        area = varea_of(parent);

        if (start < area->start)
            link = &parent->left;
        else
            link = &parent->right;
    }

    *out_parent = parent;
    *out_link = link;

    if (parent == nullptr)
        return &s_free_areas;

    area = varea_of(parent);
    if (link == &parent->right)
        return &area->link;

    return area->link.prev;
}

/*
 * Return true if a 'size'-byte, 'align'-aligned run fits inside 'area' while
 * staying within the [vstart, vend) window, storing its start in 'out_start'.
 */
static bool area_fits(
    const struct varea *area, virt_addr_t vstart, virt_addr_t vend,
    size_t size, size_t align, virt_addr_t *out_start
)
{
    virt_addr_t start;

    start = MAX(area->start, vstart);
    start = ALIGN_UP(start, align);

    // The alignment, or the run itself, wrapped around the address space
    if (start < area->start || start + size < start)
        return false;

    if (start + size > area->end || start + size > vend)
        return false;

    *out_start = start;
    return true;
}

/*
 * Find the lowest-address free area able to satisfy a 'size'-byte, 'align'-
 * aligned allocation within the [vstart, vend) window. The subtree-max
 * augmentation lets us prune whole subtrees that are too small to ever fit.
 *
 * This is the classic augmented-tree lowest-fit walk: descend left while a
 * fitting candidate may live there, otherwise try the node and its right
 * subtree, climbing back up when a branch is exhausted.
 */
static struct varea *find_free_fit(
    virt_addr_t vstart, virt_addr_t vend, size_t size, size_t align,
    virt_addr_t *out_start
)
{
    struct rb_node *node = s_free_ranges.root;
    struct varea *area;
    size_t length;

    /*
     * Prune with the worst-case alignment overhead included, so that a
     * subtree passing the size gate is never exhausted for alignment
     * reasons alone. The slack is skipped when the window is exactly the
     * request (an exact fit would spuriously fail it) and on overflow.
     */
    length = size + align - 1;
    if (length < size || vend - vstart == size)
        length = size;

    while (node) {
        area = varea_of(node);

        if (free_subtree_max(node->left) >= length && vstart < area->start) {
            node = node->left;
            continue;
        }

        if (area_fits(area, vstart, vend, size, align, out_start))
            return area;

        if (free_subtree_max(node->right) >= length) {
            node = node->right;
            continue;
        }

        // This branch is exhausted, climb to the first unvisited right subtree
        while ((node = rb_node_parent(node))) {
            area = varea_of(node);

            if (area_fits(area, vstart, vend, size, align, out_start))
                return area;

            if (free_subtree_max(node->right) >= length &&
                vstart <= area->start) {
                /*
                 * Raise the window floor past this subtree so that an
                 * exhausted branch is never re-entered: a qualifying
                 * hole lying beyond 'vend' would otherwise make this
                 * walk loop forever.
                 */
                vstart = area->start + 1;
                node = node->right;
                break;
            }
        }
    }

    return nullptr;
}

/*
 * Per-CPU reservoir of a single spare varea. Only an allocation that splits
 * a free hole in the middle needs an extra node (for the leading remnant),
 * and that split runs under s_valloc_lock where we must not call into a
 * potentially-blocking allocator. So the spare is allocated outside the
 * lock and parked here, then consumed under the lock by clip_free_area().
 */
MAKE_PER_CPU(static, struct varea*, s_spare_varea);

static struct varea *take_spare_varea(void)
{
    struct varea *spare;

    // Atomic not needed here since this is called with IRQs disabled
    spare = this_cpu_read(s_spare_varea);
    if (likely(spare != nullptr))
        this_cpu_write(s_spare_varea, nullptr);

    return spare;
}

/*
 * Carve [astart, aend) out of the free area 'fa'. The free remainder(s) are
 * clipped in place, only a split with both a leading and a trailing gap needs
 * an extra node, pulled from this CPU's spare reservoir. Returns false if that
 * spare was unavailable, leaving 'fa' untouched so the caller can bail cleanly.
 */
static bool clip_free_area(
    struct varea *fa, virt_addr_t astart, virt_addr_t aend
)
{
    bool has_lead, has_trail;

    has_lead = astart > fa->start;
    has_trail = aend < fa->end;

    if (!has_lead && !has_trail) {
        // The whole hole is consumed
        free_tree_remove(fa);
        list_remove(&fa->link);
    } else if (!has_lead) {
        // Allocation sits at the front, free remainder follows it
        fa->start = aend;
        free_ranges_tree_propagate(&fa->node, nullptr);
    } else if (!has_trail) {
        // Allocation sits at the back, free remainder precedes it
        fa->end = astart;
        free_ranges_tree_propagate(&fa->node, nullptr);
    } else {
        // Hole on both sides: 'fa' keeps the trailing part, 'spare' the leading
        struct varea *spare;

        spare = take_spare_varea();
        if (unlikely(spare == nullptr))
            return false;

        spare->start = fa->start;
        spare->end = astart;
        spare->type = VAREA_FREE;

        /*
         * Shrink 'fa' down to the trailing remnant first. 'spare' inherited
         * fa's old start, so it must be inserted only once fa's key has moved
         * up to 'aend', otherwise the two compare equal and 'spare' lands on
         * the wrong side of 'fa', corrupting the tree ordering.
         */
        fa->start = aend;
        free_ranges_tree_propagate(&fa->node, nullptr);

        rb_node_insert_aggregated(
            &spare->node, &s_free_ranges, varea_start_less,
            &free_ranges_tree_ops
        );
        list_insert_prev(&fa->link, &spare->link);
    }

    return true;
}

/*
 * Reserve a 'size'-byte, 'align'-aligned area within [start, end) and
 * return it with a freshly allocated (zeroed) info struct attached. The
 * caller fills in the type-specific fields. The returned area is a
 * VAREA_RESERVATION by default.
 *
 * A guarded reservation gets one extra trailing page that the map paths
 * never touch, so that a linear overrun faults instead of silently
 * corrupting whatever is reserved right above.
 */
static struct varea *varea_alloc_within(
    virt_addr_t start, virt_addr_t end, size_t size,
    size_t align, bool guarded, enum alloc_behavior behavior
)
{
    struct varea *area, *preload, *fa;
    struct vallocation *info;
    virt_addr_t astart, aend;
    irq_state_t irq;

    if (align == 0)
        align = PAGE_SIZE;
    BUG_ON(!IS_POWER_OF_TWO(align));
    align = MAX(align, (size_t)PAGE_SIZE);

    size = PAGE_ROUND_UP(size);
    if (unlikely(size == 0))
        return nullptr;

    if (guarded) {
        if (unlikely(size + PAGE_SIZE < size))
            return nullptr;
        size += PAGE_SIZE;
    }

    area = alloc_from_cache(&s_varea_cache, behavior);
    info = alloc_from_cache(&s_vallocation_cache, behavior | ALLOC_ZEROED);
    if (unlikely(area == nullptr || info == nullptr))
        goto out_free;

    /*
     * Preload this CPU's spare reservoir before taking the lock, so an
     * in-the-middle split can pull a node from it without allocating under the
     * lock. The peek is an advisory hint (it races with other refills), the
     * install under the lock is authoritative.
     */
    preload = nullptr;
    if (this_cpu_read(s_spare_varea) == nullptr)
        preload = alloc_from_cache(&s_varea_cache, behavior);

    irq = spin_lock_irq_save(&s_valloc_lock);

    for (;;) {
        if (preload != nullptr && this_cpu_read(s_spare_varea) == nullptr) {
            this_cpu_write(s_spare_varea, preload);
            preload = nullptr;
        }

        fa = find_free_fit(start, end, size, align, &astart);
        if (unlikely(fa == nullptr)) {
            spin_unlock_irq_restore(&s_valloc_lock, irq);
            goto out_free_preload;
        }
        aend = astart + size;

        if (likely(clip_free_area(fa, astart, aend)))
            break;

        /*
         * A mid-hole split found the reservoir drained (something on
         * this CPU consumed the spare after the preload peek). Refill
         * outside the lock and redo the fit, the tree may have changed.
         */
        spin_unlock_irq_restore(&s_valloc_lock, irq);

        preload = alloc_from_cache(&s_varea_cache, behavior);
        if (unlikely(preload == nullptr))
            goto out_free;

        irq = spin_lock_irq_save(&s_valloc_lock);
    }

    BUG_ON(!IS_ALIGNED(astart, align));
    BUG_ON(astart < start || aend > end);

    area->start = astart;
    area->end = aend;
    area->type = VAREA_RESERVATION;
    area->info = info;
    if (guarded)
        info->flags |= VALLOC_GUARDED;
    reserved_tree_insert(area);

    spin_unlock_irq_restore(&s_valloc_lock, irq);

    // The spare survived in the reservoir, release the one we couldn't install
    if (preload != nullptr)
        free(preload);

    return area;

out_free_preload:
    free(preload);
out_free:
    free(area);
    free(info);
    return nullptr;
}

typedef phys_addr_t (*area_to_phys_addr_cb_t)(
    struct varea *area, size_t pfn_idx
);

static phys_addr_t managed_area_to_phys_addr(
    struct varea *area, size_t pfn_idx
)
{
    return block_to_phys(area->info->pages[pfn_idx]);
}

static phys_addr_t reserved_area_to_phys_addr(
    struct varea *area, size_t pfn_idx
)
{
    return area->info->phys_addr + (pfn_idx << PAGE_SHIFT);
}

/*
 * Mapping and unmapping run without any page-table lock:
 *
 *  - Leaf entries: a varea's VA range is exclusively owned by the
 *    thread mapping or unmapping it, the reservation itself is the
 *    exclusion. Two threads may write different leaf entries of the
 *    same pt1 concurrently, which is fine, entries are independent
 *    atomic cells.
 *
 *  - Intermediate entries: the only shared state is the installation of
 *    a missing table, resolved per entry by ptN_cmpxchg_populate(). The
 *    loser frees its table and descends into the winner's.
 *
 *  - Intermediate tables are never freed in the kernel address space,
 *    so there is no teardown race and no deferred-free machinery.
 *
 *  - A non-present to present transition needs no TLB invalidation,
 *    which is why only the unmap paths flush.
 */

static void valloc_map_pt1(
    struct pt1 *pt1, virt_addr_t virt, virt_addr_t end,
    struct varea *area, area_to_phys_addr_cb_t to_phys, pt_prot prot
)
{
    size_t pfn_idx;

    for (; virt < end; virt += PT1_SIZE, pt1++) {
        pfn_idx = (virt - area->start) >> PAGE_SHIFT;
        pt1_exclusive_make_leaf(pt1, to_phys(area, pfn_idx), prot);
    }
}

static error_t valloc_map_pt2(
    struct pt2 *pt2, virt_addr_t virt, virt_addr_t end,
    struct varea *area, area_to_phys_addr_cb_t to_phys, pt_prot prot,
    enum alloc_behavior behavior
)
{
    struct page_block *table;
    virt_addr_t next;
    struct pt1 *pt1;

    for (; virt < end; virt = next, pt2++) {
        next = ALIGN_DOWN(virt, PT2_SIZE) + PT2_SIZE;
        if (next < virt || next > end)
            next = end;

        if (!pt2_present(pt2)) {
            table = alloc_page_table(behavior);
            if (unlikely(table == nullptr))
                return ENOMEM;

            if (!pt2_cmpxchg_populate(pt2, block_to_virt(table)))
                free_frozen_block(table);
        }

        pt1 = pt1_from_pt2(pt2, virt);
        valloc_map_pt1(pt1, virt, next, area, to_phys, prot);
    }

    return EOK;
}

static error_t valloc_map_pt3(
    struct pt3 *pt3, virt_addr_t virt, virt_addr_t end,
    struct varea *area, area_to_phys_addr_cb_t to_phys, pt_prot prot,
    enum alloc_behavior behavior
)
{
    struct page_block *table;
    virt_addr_t next;
    struct pt2 *pt2;
    error_t ret;

    for (; virt < end; virt = next, pt3++) {
        next = ALIGN_DOWN(virt, PT3_SIZE) + PT3_SIZE;
        if (next < virt || next > end)
            next = end;

        if (!pt3_present(pt3)) {
            table = alloc_page_table(behavior);
            if (unlikely(table == nullptr))
                return ENOMEM;

            if (!pt3_cmpxchg_populate(pt3, block_to_virt(table)))
                free_frozen_block(table);
        }

        pt2 = pt2_from_pt3(pt3, virt);
        ret = valloc_map_pt2(pt2, virt, next, area, to_phys, prot, behavior);
        if (is_error(ret))
            return ret;
    }

    return EOK;
}

static error_t valloc_map_pt4(
    struct pt4 *pt4, virt_addr_t virt, virt_addr_t end,
    struct varea *area, area_to_phys_addr_cb_t to_phys, pt_prot prot,
    enum alloc_behavior behavior
)
{
    struct page_block *table;
    virt_addr_t next;
    struct pt3 *pt3;
    error_t ret;

    for (; virt < end; virt = next, pt4++) {
        next = ALIGN_DOWN(virt, PT4_SIZE) + PT4_SIZE;
        if (next < virt || next > end)
            next = end;

        if (!pt4_present(pt4)) {
            table = alloc_page_table(behavior);
            if (unlikely(table == nullptr))
                return ENOMEM;

            if (!pt4_cmpxchg_populate(pt4, block_to_virt(table)))
                free_frozen_block(table);
        }

        pt3 = pt3_from_pt4(pt4, virt);
        ret = valloc_map_pt3(pt3, virt, next, area, to_phys, prot, behavior);
        if (is_error(ret))
            return ret;
    }

    return EOK;
}

static error_t valloc_map_range(
    struct varea *area, virt_addr_t start, virt_addr_t end,
    area_to_phys_addr_cb_t to_phys, pt_prot prot, enum alloc_behavior behavior
)
{
    virt_addr_t virt = start, next;
    struct page_block *table;
    struct pt5 *pt5;
    struct pt4 *pt4;
    error_t ret;

    pt5 = pt_root_from_address_space(&g_kernel_address_space, virt);

    for (; virt < end; virt = next, pt5++) {
        next = ALIGN_DOWN(virt, PT5_SIZE) + PT5_SIZE;
        if (next < virt || next > end)
            next = end;

        if (!pt5_present(pt5)) {
            table = alloc_page_table(behavior);
            if (unlikely(table == nullptr))
                return ENOMEM;

            if (!pt5_cmpxchg_populate(pt5, block_to_virt(table)))
                free_frozen_block(table);
        }

        pt4 = pt4_from_pt5(pt5, virt);
        ret = valloc_map_pt4(pt4, virt, next, area, to_phys, prot, behavior);
        if (is_error(ret))
            return ret;
    }

    return EOK;
}

static void valloc_unmap_pt1(struct pt1 *pt1, virt_addr_t virt, virt_addr_t end)
{
    for (; virt < end; virt += PT1_SIZE, pt1++)
        pt1_exclusive_clear(pt1);
}

static void valloc_unmap_pt2(struct pt2 *pt2, virt_addr_t virt, virt_addr_t end)
{
    virt_addr_t next;
    struct pt1 *pt1;

    for (; virt < end; virt = next, pt2++) {
        next = ALIGN_DOWN(virt, PT2_SIZE) + PT2_SIZE;
        if (next < virt || next > end)
            next = end;

        if (!pt2_present(pt2))
            continue;

        pt1 = pt1_from_pt2(pt2, virt);
        valloc_unmap_pt1(pt1, virt, next);
    }
}

static void valloc_unmap_pt3(struct pt3 *pt3, virt_addr_t virt, virt_addr_t end)
{
    virt_addr_t next;
    struct pt2 *pt2;

    for (; virt < end; virt = next, pt3++) {
        next = ALIGN_DOWN(virt, PT3_SIZE) + PT3_SIZE;
        if (next < virt || next > end)
            next = end;

        if (!pt3_present(pt3))
            continue;

        pt2 = pt2_from_pt3(pt3, virt);
        valloc_unmap_pt2(pt2, virt, next);
    }
}

static void valloc_unmap_pt4(struct pt4 *pt4, virt_addr_t virt, virt_addr_t end)
{
    virt_addr_t next;
    struct pt3 *pt3;

    for (; virt < end; virt = next, pt4++) {
        next = ALIGN_DOWN(virt, PT4_SIZE) + PT4_SIZE;
        if (next < virt || next > end)
            next = end;

        if (!pt4_present(pt4))
            continue;

        pt3 = pt3_from_pt4(pt4, virt);
        valloc_unmap_pt3(pt3, virt, next);
    }
}

static void valloc_unmap_range(virt_addr_t start, virt_addr_t end)
{
    virt_addr_t virt = start, next;
    struct pt5 *pt5;
    struct pt4 *pt4;

    pt5 = pt_root_from_address_space(&g_kernel_address_space, virt);

    for (; virt < end; virt = next, pt5++) {
        next = ALIGN_DOWN(virt, PT5_SIZE) + PT5_SIZE;
        if (next < virt || next > end)
            next = end;

        if (!pt5_present(pt5))
            continue;

        pt4 = pt4_from_pt5(pt5, virt);
        valloc_unmap_pt4(pt4, virt, next);
    }
}

static error_t varea_map(
    struct varea *area, pt_prot prot, enum alloc_behavior behavior
)
{
    area_to_phys_addr_cb_t to_phys = reserved_area_to_phys_addr;
    virt_addr_t end;
    error_t ret;

    if (varea_mapped(area))
        return EOK;

    if (varea_is_managed(area))
        to_phys = managed_area_to_phys_addr;

    end = varea_usable_end(area);

    ret = valloc_map_range(area, area->start, end, to_phys, prot, behavior);
    if (is_error(ret)) {
        // Roll back the leaves we managed to install before running dry
        valloc_unmap_range(area->start, end);
        tlb_invalidate_kernel_range(area->start, end);
        return ret;
    }

    area->info->flags |= VALLOC_MAPPED;
    return EOK;
}

static void varea_unmap(struct varea *area)
{
    struct vallocation *info = area->info;
    virt_addr_t end;

    if (!varea_mapped(area))
        return;

    end = varea_usable_end(area);

    valloc_unmap_range(area->start, end);
    tlb_invalidate_kernel_range(area->start, end);
    info->flags &= ~VALLOC_MAPPED;
}

// Releasing & coalescing

static void coalesce_and_insert_free(struct varea *area)
{
    struct rb_node *parent, **link;
    struct list_link *pos, *successor;
    struct varea *sibling;
    bool merged = false;

    pos = free_tree_find_links(area->start, &parent, &link);
    successor = pos->next;

    // Merge with the following area if they are adjacent
    if (successor != &s_free_areas) {
        sibling = list_entry(successor, struct varea, link);

        if (sibling->start == area->end) {
            /*
             * Extending the successor down to our start cannot break
             * the tree ordering: the range in between was reserved, so
             * no free node's key lives inside it.
             */
            sibling->start = area->start;
            free_ranges_tree_propagate(&sibling->node, nullptr);
            free(area);

            area = sibling;
            merged = true;
        }
    }

    // Merge into the preceding area if they are adjacent
    if (pos != &s_free_areas) {
        sibling = list_entry(pos, struct varea, link);

        if (sibling->end == area->start) {
            // Detach the forward-merged node before growing into it
            if (merged) {
                free_tree_remove(area);
                list_remove(&area->link);
            }

            sibling->end = area->end;
            free_ranges_tree_propagate(&sibling->node, nullptr);
            free(area);
            return;
        }
    }

    if (merged)
        return;

    area->type = VAREA_FREE;
    rb_node_insert_at_aggregated(
        &area->node, parent, link, &s_free_ranges, &free_ranges_tree_ops
    );
    list_insert_next(pos, &area->link);
}

/*
 * Past a page worth of entries the page array is delegated to valloc
 * itself: the heap tops out at BUDDY_MAX_SIZE (which would cap valloc at
 * ~2 GiB), and demands physically contiguous metadata well before that.
 * The recursion is strictly bounded, every nested level is
 * PAGE_SIZE / sizeof(void*) times smaller than the previous one.
 */
static struct page_block **alloc_page_array(
    size_t num_pages, enum alloc_behavior behavior
)
{
    size_t size;

    size = num_pages * sizeof(struct page_block*);
    if (size > PAGE_SIZE)
        return valloc(size, behavior | ALLOC_ZEROED);

    return alloc(size, behavior | ALLOC_ZEROED);
}

static void free_page_array(struct page_block **pages, size_t num_pages)
{
    size_t size;

    size = num_pages * sizeof(struct page_block*);
    if (size > PAGE_SIZE) {
        vrelease(pages);
        return;
    }

    free(pages);
}

/*
 * Tear down an area that has already been detached from the reserved tree:
 * unmap it, release any backing it owns, and fold it back into the free space.
 */
static void varea_release_detached(struct varea *area)
{
    struct vallocation *info = area->info;
    size_t num_pages;
    irq_state_t irq;

    num_pages = varea_pages(area);
    varea_unmap(area);

    if (varea_is_managed(area)) {
        free_frozen_blocks_bulk(info->pages, num_pages);
        free_page_array(info->pages, num_pages);
    }

    free(info);

    irq = spin_lock_irq_save(&s_valloc_lock);
    coalesce_and_insert_free(area);
    spin_unlock_irq_restore(&s_valloc_lock, irq);
}

// Destroy a reserved area that is still linked into the reserved tree
static void varea_destroy(struct varea *area)
{
    irq_state_t irq;

    irq = spin_lock_irq_save(&s_valloc_lock);
    reserved_tree_remove(area);
    spin_unlock_irq_restore(&s_valloc_lock, irq);

    varea_release_detached(area);
}

static void validate_physical_range(
    phys_addr_t start, size_t size, const char *caller
)
{
    const char *why;
    phys_addr_t end_rounded;
    size_t size_rounded;

    if (unlikely(!IS_PAGE_ALIGNED(start))) {
        why = "address is misaligned";
        goto out_panic;
    }

    size_rounded = PAGE_ROUND_UP(size);
    end_rounded = start + size_rounded;

    /*
     * A zero rounded size is either an empty range or one so large the
     * round-up overflowed, since aligning up can only ever land on zero.
     */
    if (unlikely(size_rounded == 0 || end_rounded < start)) {
        why = "bad size";
        goto out_panic;
    }

    // Must come after the wrap check, an overflowed end is small
    if (unlikely(end_rounded > MAX_PHYS_ADDR)) {
        why = "address is too high";
        goto out_panic;
    }

    return;

out_panic:
    panic(
        "attempting to %s() an invalid physical address "
        "0x%016llX (%zu bytes): %s", caller, start, size, why
    );
}

// Public interface

void *valloc(size_t size, enum alloc_behavior behavior)
{
    struct varea *area;
    struct vallocation *info;
    struct page_block **pages;
    size_t num_pages;
    error_t ret;

    num_pages = PAGE_ROUND_UP(size) >> PAGE_SHIFT;
    if (unlikely(num_pages == 0))
        return nullptr;

    pages = alloc_page_array(num_pages, behavior);
    if (unlikely(pages == nullptr))
        return nullptr;

    ret = alloc_typed_blocks_bulk_or_fail(
        num_pages, pages, behavior, PAGE_TYPE_VALLOC_MANAGED
    );
    if (is_error(ret)) {
        free_page_array(pages, num_pages);
        return nullptr;
    }

    area = varea_alloc_within(
        VALLOC_BASE, VALLOC_END, size, 0, true, behavior
    );
    if (unlikely(area == nullptr)) {
        free_frozen_blocks_bulk(pages, num_pages);
        free_page_array(pages, num_pages);
        return nullptr;
    }

    info = area->info;
    area->type = VAREA_MANAGED;
    info->pages = pages;
    info->caller = __builtin_return_address(0);

    ret = varea_map(area, s_managed_mappings_prot, behavior);
    if (is_error(ret)) {
        varea_destroy(area);
        return nullptr;
    }

    return varea_to_ptr(area);
}

void *vmap_reserved(
    virt_addr_t vaddr, phys_addr_t paddr, size_t size,
    pt_prot prot, enum alloc_behavior behavior
)
{
    struct varea *area;
    irq_state_t irq;
    error_t ret;

    validate_physical_range(paddr, size, __func__);

    irq = spin_lock_irq_save(&s_valloc_lock);
    area = reserved_area_get(vaddr, __func__);
    spin_unlock_irq_restore(&s_valloc_lock, irq);

    BUG_ON(varea_is_managed(area));

    if (unlikely(area->info->flags & VALLOC_MAPPED))
        panic(
            "vmap_reserved: attempt to map an already mapped area "
            "[0x%016zX - 0x%016zX]", area->start, area->end
        );
    if (unlikely(varea_usable_size(area) != PAGE_ROUND_UP(size)))
        panic(
            "vmap_reserved: mapping size %zu (%zu rounded) != "
            "reservation size %zu", size, PAGE_ROUND_UP(size),
            varea_usable_size(area)
        );

    area->info->phys_addr = paddr;
    ret = varea_map(area, prot, behavior);
    if (is_error(ret))
        return nullptr;

    return (void*)vaddr;
}

virt_addr_t vreserve(size_t size)
{
    return vreserve_within(VALLOC_BASE, VALLOC_END, size);
}

void *vreserve_and_map(size_t size, phys_addr_t phys, pt_prot prot)
{
    struct varea *area;
    struct vallocation *info;
    error_t ret;

    validate_physical_range(phys, size, __func__);

    area = varea_alloc_within(
        VALLOC_BASE, VALLOC_END, size, 0, true, ALLOC_GENERIC
    );
    if (unlikely(area == nullptr))
        return nullptr;

    info = area->info;
    info->phys_addr = phys;
    info->caller = __builtin_return_address(0);

    ret = varea_map(area, prot, ALLOC_GENERIC);
    if (is_error(ret)) {
        varea_destroy(area);
        return nullptr;
    }

    return varea_to_ptr(area);
}

virt_addr_t vreserve_aligned_within(
    virt_addr_t start, virt_addr_t end, size_t align, size_t size
)
{
    struct varea *area;

    area = varea_alloc_within(start, end, size, align, true, ALLOC_GENERIC);
    if (unlikely(area == nullptr))
        return 0;

    area->info->caller = __builtin_return_address(0);
    return area->start;
}

error_t vreserve_permanent(virt_addr_t start, virt_addr_t end, const char *what)
{
    struct varea *area;
    error_t ret = EINVAL;

    if (unlikely(end <= start))
        goto out_warn;
    if (unlikely(!IS_PAGE_ALIGNED(start) || !IS_PAGE_ALIGNED(end)))
        goto out_warn;

    area = varea_alloc_within(
        start, end, end - start, 0, false, ALLOC_GENERIC
    );
    if (unlikely(area == nullptr)) {
        ret = ENOMEM;
        goto out_warn;
    }

    area->info->flags |= VALLOC_PERMANENT;
    area->info->what = what;
    return EOK;

out_warn:
    // Be loud because permanent reservations are usually very important
    pr_warn(
        "unable to reserve %s [0x%016zX -> 0x%016zX]: %d\n",
        what, start, end, ret
    );
    return ret;
}

void vrelease(void *ptr)
{
    virt_addr_t addr = (virt_addr_t)ptr;
    struct varea *area;
    irq_state_t irq;

    if (ptr == nullptr)
        return;

    irq = spin_lock_irq_save(&s_valloc_lock);

    area = reserved_area_get(addr, __func__);
    reserved_tree_remove(area);

    spin_unlock_irq_restore(&s_valloc_lock, irq);

    varea_release_detached(area);
}

static void* INIT_CODE valloc_pte_alloc_or_die(void)
{
    struct page_block *table;

    table = alloc_page_table(ALLOC_GENERIC);
    if (unlikely(table == nullptr))
        panic("valloc: out of memory while preallocating page tables");

    return block_to_virt(table);
}

/*
 * Eagerly populate the topmost non-folded page-table level across the whole
 * window. That keeps the entries covering it stable and shareable, so the
 * mapping fast path only ever has to allocate the levels below it.
 *
 * Any of the upper levels may be folded (we only assume the bottom two, pt2 and
 * pt1, are always real). Folded levels are descended through without allocating
 * anything, the first real level we reach is the one we populate and stop at.
 */

static void INIT_CODE valloc_prealloc_pt2(
    struct pt2 *pt2, virt_addr_t virt, virt_addr_t end
)
{
    virt_addr_t next;

    for (; virt < end; virt = next, pt2++) {
        next = ALIGN_DOWN(virt, PT2_SIZE) + PT2_SIZE;
        if (next < virt || next > end)
            next = end;

        if (!pt2_present(pt2))
            pt2_exclusive_populate(pt2, valloc_pte_alloc_or_die());
    }
}

static void INIT_CODE valloc_prealloc_pt3(
    struct pt3 *pt3, virt_addr_t virt, virt_addr_t end
)
{
    virt_addr_t next;

    for (; virt < end; virt = next, pt3++) {
        next = ALIGN_DOWN(virt, PT3_SIZE) + PT3_SIZE;
        if (next < virt || next > end)
            next = end;

        if (pt3_is_folded()) {
            valloc_prealloc_pt2(pt2_from_pt3(pt3, virt), virt, next);
        } else if (!pt3_present(pt3)) {
            pt3_exclusive_populate(pt3, valloc_pte_alloc_or_die());
        }
    }
}

static void INIT_CODE valloc_prealloc_pt4(
    struct pt4 *pt4, virt_addr_t virt, virt_addr_t end
)
{
    virt_addr_t next;

    for (; virt < end; virt = next, pt4++) {
        next = ALIGN_DOWN(virt, PT4_SIZE) + PT4_SIZE;
        if (next < virt || next > end)
            next = end;

        if (pt4_is_folded()) {
            valloc_prealloc_pt3(pt3_from_pt4(pt4, virt), virt, next);
        } else if (!pt4_present(pt4)) {
            pt4_exclusive_populate(pt4, valloc_pte_alloc_or_die());
        }
    }
}

static void INIT_CODE valloc_prealloc_root(void)
{
    virt_addr_t virt = VALLOC_BASE, end = VALLOC_END, next;
    struct pt5 *pt5;

    pt5 = pt_root_from_address_space(&g_kernel_address_space, virt);

    for (; virt < end; virt = next, pt5++) {
        next = ALIGN_DOWN(virt, PT5_SIZE) + PT5_SIZE;
        if (next < virt || next > end)
            next = end;

        if (pt5_is_folded()) {
            valloc_prealloc_pt4(pt4_from_pt5(pt5, virt), virt, next);
        } else if (!pt5_present(pt5)) {
            pt5_exclusive_populate(pt5, valloc_pte_alloc_or_die());
        }
    }
}

void INIT_CODE valloc_setup(void)
{
    struct varea *whole;

    s_managed_mappings_prot = pt_prot_from_vm_prot(
        VM_PROT_KERNEL | VM_PROT_READ | VM_PROT_WRITE
    );

    alloc_cache_init(&s_varea_cache, "varea", sizeof(struct varea), 0);
    alloc_cache_init(
        &s_vallocation_cache, "vallocation",
        sizeof(struct vallocation), 0
    );

    valloc_prealloc_root();

    whole = alloc_from_cache(&s_varea_cache, ALLOC_GENERIC);
    if (unlikely(whole == nullptr))
        panic("valloc: unable to allocate the initial free area");

    /*
     * valloc owns the entire kernel address-space arena, not just the vmalloc
     * window. Subsystems carve their fixed regions (direct map, memory map,
     * kernel binary, ...) out of it as permanent reservations at the PRE edge
     * of VALLOC_AVAILABLE. plain valloc()/module allocations request from their
     * own sub-windows. Seed the whole canonical higher half as one free hole.
     */
    whole->start = g_direct_map_base;
    whole->end = KERNEL_VA_END;

    free_tree_insert(whole);
    list_insert_next(&s_free_areas, &whole->link);
}

/*
 * Sizes that land on a whole number of units are printed as one, everything
 * else carries two decimals. Both forms are six columns wide: three digits, a
 * dot and two decimals is as wide as a size gets, since anything larger
 * promotes to the next unit up.
 */
static void INIT_CODE dump_one_area(
    virt_addr_t start, virt_addr_t end, const char *what
)
{
    struct human_size size;

    size_to_human(end - start, &size);

    if (size.hundredths) {
        pr_info(
            "  [0x%016zX - 0x%016zX] %3zu.%02zu %-3s  %s\n",
            start, end, size.value, size.hundredths, size.unit, what
        );
    } else {
        pr_info(
            "  [0x%016zX - 0x%016zX] %6zu %-3s  %s\n",
            start, end, size.value, size.unit, what
        );
    }
}

static void INIT_CODE dump_hole_up_to(
    virt_addr_t *at, virt_addr_t end
)
{
    if (end <= *at)
        return;

    dump_one_area(*at, end, "(gap)");
    *at = end;
}

static void INIT_CODE dump_next_area(
    virt_addr_t *at, virt_addr_t start, virt_addr_t end,
    const char *what
)
{
    dump_hole_up_to(at, start);
    dump_one_area(start, end, what);
    *at = end;
}

/*
 * Print every permanent reservation in address order, giving an early map of
 * the kernel half of the address space. The valloc window is not a reservation
 * of its own, so it's woven into the listing at the point where it belongs.
 *
 * Anything neither reserved nor part of the window is dumped as a hole, so the
 * listing covers the arena end to end and the trailing total says how much of
 * it is still up for grabs.
 */
static error_t INIT_CODE dump_initial_permanent_areas(void)
{
    virt_addr_t at = g_direct_map_base;
    bool window_dumped = false;
    struct rb_node *node;
    struct varea *area;

    pr_info("initial kernel address space layout:\n");

    for (node = rb_first(&s_reserved_ranges); node; node = rb_next(node)) {
        area = varea_of(node);

        if (!(area->info->flags & VALLOC_PERMANENT))
            continue;

        if (!window_dumped && area->start >= VALLOC_BASE) {
            dump_next_area(&at, VALLOC_BASE, VALLOC_END, "valloc window");
            window_dumped = true;
        }

        dump_next_area(&at, area->start, area->end, area->info->what);
    }

    if (!window_dumped)
        dump_next_area(&at, VALLOC_BASE, VALLOC_END, "valloc window");

    dump_hole_up_to(&at, KERNEL_VA_END);
    return EOK;
}

/*
 * Register as POST, we expect all permanent reservations to appear at the
 * PRE edge of VALLOC_AVAILABLE.
 */
INIT_CALL_POST(VALLOC_AVAILABLE, dump_initial_permanent_areas);
