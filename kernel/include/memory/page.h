#pragma once

/*
 * Invariants:
 *
 *  1. Every order > 0 allocation is compound, tail pages link to page 0
 *     of their block. Non-compound multi-order state does not exist.
 *
 *  2. page->meta is the only field a lockless reader may touch. All
 *     access to it, including by exclusive owners, goes through relaxed
 *     atomics, so mixed plain/atomic access to the same object never
 *     arises. A single relaxed load is self-consistent:
 *
 *         bit 0        : 1 = tail page, 0 = page 0 of a block
 *         tail (bit0=1): bits 63:1 = pointer to page 0 of the block
 *         block(bit0=0): bits 7:1 = enum page_type, bits 13:8 = order
 *
 *     Relaxed suffices because meta has no ordering relationship with
 *     other memory, publication of a fresh allocation to another CPU
 *     must go through a synchronizing operation (a lock, or a release
 *     store of the owning pointer) as usual.
 *
 *  3. Type is chosen by the caller and stamped once, at allocation
 *     time: alloc_typed_block() writes the final enum page_type into
 *     meta during prep. A block's type is immutable from alloc to free.
 *
 *  4. Order is stored for every block and is immutable from alloc to
 *     free.
 *
 *  5. The refcount exists only inside the refcounted union payload.
 *     Frozen types physically have no refcount field.
 */

#include <common/atomic.h>
#include <common/types.h>
#include <common/list.h>
#include <common/slist.h>
#include <common/bit.h>

#include <memory/io.h>
#include <memory/bug.h>
#include <arch/constants.h>
#include <spinlock.h>

/*
 * Page type values are split into two main categories:
 * 1. Frozen (not participating in reference counting, mostly for low level
 *            subsystems, but public buddy api allows allocating frozen blocks
 *            as well if needed)
 * 2. Refcounted (the rest of the pages)
 *
 * The first half of the enum (before PAGE_TYPE_REFCOUNTED_BASE) is category 1,
 * the rest is category 2. Make sure to follow this rule when adding new values
 * to the enum.
 */
enum page_type : u8 {
    /*
     * Hardware-reserved or early-allocated for permanent kernel data.
     * Must be kept as 0 so that zero-initialized struct pages and the
     * memory map decode as RESERVED, order 0, non-tail, which is inert
     * by default.
     */
    PAGE_TYPE_RESERVED = 0,

    /*
     * Free, owned by the buddy allocator
     */
    PAGE_TYPE_BUDDY,

    /*
     * Anonymous frozen page owned by a caller that declined to declare
     * a concrete type (private driver pools and the like). A terminal
     * state handed out by alloc_frozen_block(), never re-stamped.
     */
    PAGE_TYPE_GENERIC_FROZEN,

    // Slab page owned by the kernel heap (small objects)
    PAGE_TYPE_KHEAP,

    /*
     * Direct multi-order kheap allocation (large objects).
     */
    PAGE_TYPE_KHEAP_LARGE,

    // Page-table page
    PAGE_TYPE_PTDESC,

    /*
     * Valloc-internal page type used for "managed" (backed by physical
     * memory) allocations.
     */
    PAGE_TYPE_VALLOC_MANAGED,

    /*
     * Frozen page types ^^^
     * --------------------------
     * Refcounted page types vvv
     */

    /*
     * Anonymous reference-counted page allocated by default via the public
     * buddy allocator api like alloc_block() etc.
     */
    PAGE_TYPE_GENERIC,
    PAGE_TYPE_REFCOUNTED_BASE = PAGE_TYPE_GENERIC,

    // Anonymous userspace memory
    PAGE_TYPE_ANON,

    // File-backed page cache
    PAGE_TYPE_PAGE_CACHE,
};

static inline bool page_type_is_refcounted(enum page_type type)
{
    return type >= PAGE_TYPE_REFCOUNTED_BASE;
}

static inline bool page_type_is_buddy_managed(enum page_type type)
{
    return type == PAGE_TYPE_BUDDY || type == PAGE_TYPE_RESERVED;
}

#define META_TAIL_BIT BIT_OF_TYPE(ptr_t, 0)
#define META_TYPE_MASK MAKE_BIT_MASK_OF_TYPE(7, 1, ptr_t)
#define META_ORDER_MASK MAKE_BIT_MASK_OF_TYPE(13, 8, ptr_t)

// Physical meta field width limit, the buddy caps far lower (BUDDY_MAX_ORDER)
#define META_MAX_ORDER BIT_FIELD_MAX(META_ORDER_MASK)

#define META_IS_TAIL(meta) (((meta) & META_TAIL_BIT) != 0)

#define META_TO_TYPE(meta) \
    ((enum page_type)BIT_FIELD_READ(meta, META_TYPE_MASK))

#define META_TO_ORDER(meta) ((u8)BIT_FIELD_READ(meta, META_ORDER_MASK))

#define META_TO_BLOCK_PTR(meta) ((meta) & ~META_TAIL_BIT)

#define META_MAKE_BLOCK(type, order)          \
    (BIT_FIELD_MAKE(META_TYPE_MASK, (type)) | \
     BIT_FIELD_MAKE(META_ORDER_MASK, (order)))

#define META_MAKE_TAIL(block) ((ptr_t)(block) | META_TAIL_BIT)

struct buddy_page {
    // Freelist for this block's order
    struct list_link link;
};

struct alloc_cache;

struct kheap_page {
    // Cache membership: partial / full / empty slab list
    struct list_link link;
    struct alloc_cache *cache;
    struct slist_head freelist;
    u16 num_allocated_objects;
};

struct ptdesc_page {
    /*
     * Serializes racing installs into this table's entries (user
     * address spaces only). Teardown never takes it: removal of an
     * entry has a single owner, and a table is freed only once no
     * live range intersects its span, at which point it is empty by
     * construction.
     */
    struct spinlock lock;
};

// Base type for every reference counted "page_type"
struct refcounted_page {
    u32 num_references;
};

struct anon_page {
    struct refcounted_page base;
};

struct page_cache_page {
    struct refcounted_page base;
};

struct page {
    /*
     * Encoding at the top of this file. Written only by the buddy
     * (alloc, free, tail link setup). Access via the helpers below
     * only.
     */
    ptr_t meta;

    union {
        // Frozen page types
        struct buddy_page buddy;
        struct kheap_page kheap;
        struct ptdesc_page ptdesc;

        // Reference counted page types (all start with a refcounted base)
        struct refcounted_page refcounted;
        struct anon_page anon;
        struct page_cache_page page_cache;
    };
};
BUILD_BUG_ON_WITH_MSG(sizeof(struct page) > 64, "struct page is too large");
BUILD_BUG_ON_WITH_MSG(alignof(struct page) < 2, "meta tail bit needs bit 0");

/*
 * Head page of a (possibly multi-order) allocation. Allocators
 * only return and accept "head" pages. Use page_to_block() to
 * convert an arbitrary page to its allocation head.
 */
struct page_block {
    struct page page_0;
};
BUILD_BUG_ON_WITH_MSG(offsetof(struct page_block, page_0) != 0,
                      "page 0 not at the start of a page block");

#define block_to_page(b) (_Generic((b),                \
    const struct page_block*: (const struct page*)(b), \
          struct page_block*: (struct page*)(b)))

static inline ptr_t page_meta(const struct page *page)
{
    return atomic_load_relaxed(&page->meta);
}

static inline bool page_is_tail(const struct page *page)
{
    return META_IS_TAIL(page_meta(page));
}

static inline enum page_type page_type(const struct page *page)
{
    ptr_t meta;

    meta = page_meta(page);
    MM_BUG_ON(META_IS_TAIL(meta));

    return META_TO_TYPE(meta);
}

static inline u8 block_order(const struct page_block *block)
{
    ptr_t meta;

    meta = atomic_load_relaxed(&block->page_0.meta);
    MM_BUG_ON(META_IS_TAIL(meta));

    return META_TO_ORDER(meta);
}

static inline ptr_t page_to_block_ptr(const struct page *page)
{
    ptr_t meta;

    meta = page_meta(page);
    if (META_IS_TAIL(meta))
        return META_TO_BLOCK_PTR(meta);

    // Pages of these types must never reach a resolution path
    MM_BUG_ON(page_type_is_buddy_managed(META_TO_TYPE(meta)));

    return (ptr_t)page;
}

#define page_to_block(p) (_Generic((p),                                  \
    const struct page *: (const struct page_block *)page_to_block_ptr(p),\
          struct page *: (struct page_block *)page_to_block_ptr(p)))

extern struct page *g_memory_map;

#define PHYS_ADDR_TO_PFN(phys_addr) ((phys_addr) >> PAGE_SHIFT)
#define PFN_TO_PHYS_ADDR(pfn) ((phys_addr_t)(pfn) << PAGE_SHIFT)

static inline struct page *pfn_to_page(phys_addr_t pfn)
{
    return &g_memory_map[pfn];
}

static inline phys_addr_t page_to_pfn(const struct page *page)
{
    return (phys_addr_t)(page - g_memory_map);
}

static inline struct page *phys_to_page(phys_addr_t addr)
{
    return pfn_to_page(PHYS_ADDR_TO_PFN(addr));
}

static inline phys_addr_t page_to_phys(const struct page *page)
{
    return PFN_TO_PHYS_ADDR(page_to_pfn(page));
}

static inline void *page_to_virt(const struct page *page)
{
    return phys_to_virt(page_to_phys(page));
}

static inline struct page *virt_to_page(void *addr)
{
    return phys_to_page(virt_to_phys(addr));
}

static inline phys_addr_t block_to_phys(const struct page_block *block)
{
    return page_to_phys(block_to_page(block));
}

static inline void *block_to_virt(const struct page_block *block)
{
    return page_to_virt(block_to_page(block));
}

static inline struct page_block *virt_to_block(void *addr)
{
    return page_to_block(virt_to_page(addr));
}

static inline enum page_type block_type(const struct page_block *block)
{
    return page_type(block_to_page(block));
}

static inline struct kheap_page *block_kheap(struct page_block *block)
{
    MM_BUG_ON(block_type(block) != PAGE_TYPE_KHEAP);
    return &block_to_page(block)->kheap;
}

static inline struct ptdesc_page *block_ptdesc(struct page_block *block)
{
    MM_BUG_ON(block_type(block) != PAGE_TYPE_PTDESC);
    return &block_to_page(block)->ptdesc;
}

static inline struct refcounted_page *block_refcounted(
    struct page_block *block
)
{
    MM_BUG_ON(!page_type_is_refcounted(block_type(block)));
    return &block_to_page(block)->refcounted;
}

static inline u32 block_num_references(const struct page_block *block)
{
    const struct page *page = block_to_page(block);

    MM_BUG_ON(!page_type_is_refcounted(page_type(page)));
    return atomic_load_relaxed(&page->refcounted.num_references);
}

static inline bool block_is_kernel_heap(const struct page_block *block)
{
    enum page_type type = block_type(block);

    return type == PAGE_TYPE_KHEAP || type == PAGE_TYPE_KHEAP_LARGE;
}
