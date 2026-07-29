#define MSG_FMT(msg) "buddy: " msg

#include <common/types.h>
#include <common/bit.h>
#include <common/format.h>
#include <common/string.h>
#include <common/align.h>
#include <common/minmax.h>
#include <common/list.h>
#include <common/atomic.h>

#include <boot/alloc.h>
#include <memory/buddy.h>
#include <memory/page.h>
#include <memory/units.h>
#include <private/buddy.h>
#include <private/memory.h>

#include <free_after_init.h>
#include <bug.h>
#include <spinlock.h>

#define BUDDY_NUM_ORDERS (BUDDY_MAX_ORDER + 1)

BUILD_BUG_ON_WITH_MSG(
    BUDDY_MAX_ORDER > META_MAX_ORDER,
    "Buddy order cap exceeds meta order field width"
);
BUILD_BUG_ON_WITH_MSG(
    PAGE_TYPE_RESERVED != 0,
    "Zeroed struct page must decode as reserved"
);

struct free_area {
    struct list_link free_list;
    size_t num_free;
};

static struct buddy_context {
    struct free_area free_areas[BUDDY_NUM_ORDERS];
    struct spinlock lock;
} s_ctx;

static inline void page_set_meta(struct page *page, ptr_t meta)
{
    atomic_store_relaxed(&page->meta, meta);
}

/*
 * s_ctx.lock must be held. Dissolves the block back into raw buddy
 * pages, the mirror of prep_block().
 */
static void do_free_block(struct page_block *block)
{
    phys_addr_t pfn, buddy_pfn;
    struct page *page, *buddy;
    u8 order;

    page = block_to_page(block);
    order = block_order(block);
    pfn = page_to_pfn(page);

    while (order < BUDDY_MAX_ORDER) {
        buddy_pfn = pfn ^ BIT_PHYS(order);
        buddy = pfn_to_page(buddy_pfn);

        if (page_meta(buddy) != META_MAKE_BLOCK(PAGE_TYPE_BUDDY, order))
            break;

        list_remove(&buddy->buddy.link);
        s_ctx.free_areas[order].num_free--;

        pfn &= ~BIT_PHYS(order);
        page = pfn_to_page(pfn);

        order++;
    }

    page_set_meta(page, META_MAKE_BLOCK(PAGE_TYPE_BUDDY, order));
    list_insert_next(
        &s_ctx.free_areas[order].free_list, &page->buddy.link
    );
    s_ctx.free_areas[order].num_free++;
}

/*
 * s_ctx.lock must be held. Returns the raw head page with its meta
 * still holding the BUDDY pattern, the caller stamps ownership via
 * prep_block(), which does not need the lock.
 */
static struct page *grab_free_pages(u8 order)
{
    struct page *page, *buddy;
    phys_addr_t pfn;
    u8 cur_order;

    if (order > BUDDY_MAX_ORDER)
        return nullptr;

    for (cur_order = order; cur_order < BUDDY_NUM_ORDERS; cur_order++) {
        if (s_ctx.free_areas[cur_order].num_free)
            break;
    }

    if (cur_order == BUDDY_NUM_ORDERS)
        return nullptr;

    page = list_first_entry(
        &s_ctx.free_areas[cur_order].free_list, struct page, buddy.link
    );
    MM_BUG_ON(page_meta(page) != META_MAKE_BLOCK(PAGE_TYPE_BUDDY, cur_order));

    list_remove(&page->buddy.link);
    s_ctx.free_areas[cur_order].num_free--;

    pfn = page_to_pfn(page);

    while (cur_order > order) {
        cur_order--;

        buddy = pfn_to_page(pfn + BIT_PHYS(cur_order));
        page_set_meta(buddy, META_MAKE_BLOCK(PAGE_TYPE_BUDDY, cur_order));

        list_insert_next(
            &s_ctx.free_areas[cur_order].free_list, &buddy->buddy.link
        );
        s_ctx.free_areas[cur_order].num_free++;
    }

    return page;
}

/*
 * Stamp ownership of a grabbed block: the caller's final type and order
 * on page 0, tail links on every interior page, the initial reference
 * for the refcounted class. The block is exclusively owned here, so no
 * lock and relaxed stores suffice, publication to another CPU is the
 * caller's synchronization problem, as always.
 */
static struct page_block *prep_block(
    struct page *page, u8 order, enum page_type type,
    enum alloc_behavior behavior
)
{
    struct page_block *block;
    size_t num_pages, i;

    MM_BUG_ON(page_type_is_buddy_managed(type));

    page_set_meta(page, META_MAKE_BLOCK(type, order));
    block = page_to_block(page);

    num_pages = BIT_PHYS(order);
    for (i = 1; i < num_pages; i++)
        page_set_meta(&page[i], META_MAKE_TAIL(block));

    if (page_type_is_refcounted(type))
        atomic_store_relaxed(&page->refcounted.num_references, 1);

    if (behavior & ALLOC_ZEROED)
        memzero(page_to_virt(page), PAGE_SIZE << order);

    return block;
}

struct page_block *alloc_typed_block(
    u8 order, enum alloc_behavior behavior, enum page_type type
)
{
    struct page *page;
    irq_state_t state;

    state = spin_lock_irq_save(&s_ctx.lock);
    page = grab_free_pages(order);
    spin_unlock_irq_restore(&s_ctx.lock, state);

    if (page == nullptr)
        return nullptr;

    return prep_block(page, order, type, behavior);
}

struct page_block *alloc_block(u8 order, enum alloc_behavior behavior)
{
    return alloc_typed_block(order, behavior, PAGE_TYPE_GENERIC);
}

struct page_block *alloc_frozen_block(u8 order, enum alloc_behavior behavior)
{
    return alloc_typed_block(order, behavior, PAGE_TYPE_GENERIC_FROZEN);
}

struct page_block *alloc_page_table(enum alloc_behavior behavior)
{
    struct page_block *block;
    struct ptdesc_page *ptdesc;

    block = alloc_typed_block(
        0, behavior | ALLOC_ZEROED, PAGE_TYPE_PTDESC
    );
    if (unlikely(block == nullptr))
        return nullptr;

    ptdesc = block_ptdesc(block);
    spin_lock_init(&ptdesc->lock);

    return block;
}

void free_frozen_block(struct page_block *block)
{
    irq_state_t state;

    MM_BUG_ON(page_type_is_refcounted(block_type(block)));

    state = spin_lock_irq_save(&s_ctx.lock);
    do_free_block(block);
    spin_unlock_irq_restore(&s_ctx.lock, state);
}

void block_ref(struct page_block *block)
{
    struct page *page = block_to_page(block);
    u32 old;

    MM_BUG_ON(!page_type_is_refcounted(page_type(page)));

    old = atomic_fetch_add(
        &page->refcounted.num_references, 1, MO_RELAXED
    );
    MM_BUG_ON(old == 0);
}

/*
 * Release decrement so this owner's writes happen before the free,
 * acquire fence on the final reference so the freeing thread observes
 * every owner's writes before the block is reused.
 */
static bool block_drop_reference(struct page_block *block)
{
    struct refcounted_page *refcounted;
    u32 old;

    refcounted = block_refcounted(block);
    old = atomic_fetch_sub(
        &refcounted->num_references, 1, MO_RELEASE
    );
    MM_BUG_ON(old == 0);

    if (old != 1)
        return false;

    barrier_acquire();
    return true;
}

void block_unref(struct page_block *block)
{
    irq_state_t state;

    if (!block_drop_reference(block))
        return;

    state = spin_lock_irq_save(&s_ctx.lock);
    do_free_block(block);
    spin_unlock_irq_restore(&s_ctx.lock, state);
}

size_t alloc_typed_blocks_bulk(
    size_t count, struct page_block **blocks, enum alloc_behavior behavior,
    enum page_type type
)
{
    size_t i, num_grabbed = 0, num_populated = 0;
    struct list_link grabbed;
    struct page *page;
    irq_state_t state;

    list_init(&grabbed);

    state = spin_lock_irq_save(&s_ctx.lock);

    for (i = 0; i < count; i++) {
        if (blocks[i] != nullptr) {
            num_populated++;
            continue;
        }

        page = grab_free_pages(0);
        if (unlikely(page == nullptr))
            break;

        /*
         * The link is exclusively ours the moment the page leaves
         * the freelist, thread the raw grabs through it so that
         * prep, and in particular the memzero for ALLOC_ZEROED,
         * can run with irqs back on.
         */
        list_insert_prev(&grabbed, &page->buddy.link);
        num_grabbed++;
        num_populated++;
    }

    spin_unlock_irq_restore(&s_ctx.lock, state);

    for (i = 0; num_grabbed && i < count; i++) {
        if (blocks[i] != nullptr)
            continue;

        page = list_first_entry(&grabbed, struct page, buddy.link);

        // Prep overwrites the link, pop before prepping
        list_remove(&page->buddy.link);
        num_grabbed--;

        blocks[i] = prep_block(page, 0, type, behavior);
    }

    return num_populated;
}

size_t alloc_blocks_bulk(
    size_t count, struct page_block **blocks, enum alloc_behavior behavior
)
{
    return alloc_typed_blocks_bulk(count, blocks, behavior, PAGE_TYPE_GENERIC);
}

error_t alloc_blocks_bulk_or_fail(
    size_t count, struct page_block **blocks, enum alloc_behavior behavior
)
{
    return alloc_typed_blocks_bulk_or_fail(
        count, blocks, behavior, PAGE_TYPE_GENERIC
    );
}

/*
 * The lock is held across the whole walk: unlike the alloc side there
 * is no prep or memzero here, the per-block work under the lock is
 * bounded by the merge probe depth.
 */
void free_blocks_bulk(struct page_block **blocks, size_t count)
{
    irq_state_t state;
    size_t i;

    state = spin_lock_irq_save(&s_ctx.lock);

    for (i = 0; i < count; i++) {
        if (blocks[i] == nullptr)
            continue;

        if (block_drop_reference(blocks[i]))
            do_free_block(blocks[i]);

        blocks[i] = nullptr;
    }

    spin_unlock_irq_restore(&s_ctx.lock, state);
}

/*
 * Frozen counterpart of free_blocks_bulk(): the blocks carry no refcount,
 * so each is freed outright.
 */
void free_frozen_blocks_bulk(struct page_block **blocks, size_t count)
{
    irq_state_t state;
    size_t i;

    state = spin_lock_irq_save(&s_ctx.lock);

    for (i = 0; i < count; i++) {
        if (blocks[i] == nullptr)
            continue;

        MM_BUG_ON(page_type_is_refcounted(block_type(blocks[i])));
        do_free_block(blocks[i]);

        blocks[i] = nullptr;
    }

    spin_unlock_irq_restore(&s_ctx.lock, state);
}

// Memmap population granularity, see kernel_memory_setup_one()
#define BUDDY_WINDOW_PAGES PHYS_ADDR_TO_PFN(BUDDY_MAX_SIZE)
#define INIT_PFN_NONE ((phys_addr_t)-1)

static void INIT_CODE zero_memmap_entries(
    phys_addr_t pfn_start, phys_addr_t pfn_end
)
{
    if (pfn_start >= pfn_end)
        return;

    memzero(
        pfn_to_page(pfn_start),
        (pfn_end - pfn_start) * sizeof(struct page)
    );
}

/*
 * Zero the memmap entries of a non-RAM gap, clipped to the populated
 * windows: coverage extends ALIGN_UP from the previous range and
 * ALIGN_DOWN from the next one, whole windows in between are not
 * mapped and must not be touched.
 */
static void INIT_CODE zero_memmap_gap(
    phys_addr_t prev_end_pfn, phys_addr_t start_pfn
)
{
    phys_addr_t hi, lo;

    hi = MIN(ALIGN_UP(prev_end_pfn, BUDDY_WINDOW_PAGES), start_pfn);
    lo = MAX(ALIGN_DOWN(start_pfn, BUDDY_WINDOW_PAGES), prev_end_pfn);

    // Both ranges share one populated stretch
    if (hi >= lo) {
        zero_memmap_entries(prev_end_pfn, start_pfn);
        return;
    }

    zero_memmap_entries(prev_end_pfn, hi);
    zero_memmap_entries(lo, start_pfn);
}

/*
 * Init-time free: stamp and insert without merge probing. The carve
 * loop emits maximally-aligned blocks, so a block's buddy is never
 * another free block of the same order: had both halves of the merged
 * block been free and aligned, the carve would have emitted the
 * merged block instead. Ranges never abut either, the boot memory map
 * iteration merges adjacent free ranges.
 */
static void INIT_CODE init_free_block(struct page *page, u8 order)
{
    page_set_meta(page, META_MAKE_BLOCK(PAGE_TYPE_BUDDY, order));
    list_insert_next(
        &s_ctx.free_areas[order].free_list, &page->buddy.link
    );
    s_ctx.free_areas[order].num_free++;
}

// End pfn of the previously walked boot range
static phys_addr_t INIT_DATA s_init_pfn_cursor;

static void INIT_CODE buddy_init_ram_range(
    phys_addr_t p_start, phys_addr_t p_end, void *ctx_ptr
)
{
    struct boot_alloc_for_each_ctx *ctx = ctx_ptr;
    phys_addr_t pfn_start, pfn_end, p_cur = p_start;
    struct page *page;
    u8 order;
    u64 next_size;

    // The gap zeroing below depends on both
    MM_BUG_ON(!IS_ALIGNED(p_start, PAGE_SIZE));
    MM_BUG_ON(!IS_ALIGNED(p_end, PAGE_SIZE));

    pfn_start = PHYS_ADDR_TO_PFN(p_start);
    pfn_end = PHYS_ADDR_TO_PFN(p_end);

    if (s_init_pfn_cursor == INIT_PFN_NONE) {
        zero_memmap_entries(
            ALIGN_DOWN(pfn_start, BUDDY_WINDOW_PAGES), pfn_start
        );
    } else {
        // Ranges must arrive sorted and non-overlapping
        MM_BUG_ON(pfn_start < s_init_pfn_cursor);
        zero_memmap_gap(s_init_pfn_cursor, pfn_start);
    }
    s_init_pfn_cursor = pfn_end;

    /*
     * Reserved ranges only need their entries to decode as RESERVED,
     * which zeroed meta does. Free RAM entries are deliberately left
     * uninitialized, heads excepted.
     */
    if (!ctx->is_free) {
        zero_memmap_entries(pfn_start, pfn_end);
        return;
    }

    while (p_cur < p_end) {
        order = 0;
        while (order < BUDDY_MAX_ORDER) {
            next_size = PAGE_SIZE * BIT_PHYS(order + 1);

            if (!IS_ALIGNED(p_cur, next_size))
                break;
            if (p_cur + next_size > p_end)
                break;

            order++;
        }

        page = phys_to_page(p_cur);
        init_free_block(page, order);

        p_cur += PAGE_SIZE * BIT_PHYS(order);
    }
}

static void INIT_CODE buddy_dump_info(void)
{
    size_t i;
    struct human_size hs;
    char human_order[5];

    pr_info("initial allocator state below\n");
    pr_info("order: ");
    for (i = 0; i < BUDDY_NUM_ORDERS; i++) {
        size_to_human_short(order_to_bytes(i), &hs);
        snprintf(human_order, sizeof(human_order), "%zu%c", hs.value, *hs.unit);
        pr_cont("[%-4s] ", human_order);
    }
    pr_cont("\n");

    pr_info("free:  ");
    for (i = 0; i < BUDDY_NUM_ORDERS; i++)
        pr_cont(" %-6zu", s_ctx.free_areas[i].num_free);
    pr_cont("\n");
}

void INIT_CODE buddy_setup(void)
{
    size_t i;

    spin_lock_init(&s_ctx.lock);

    for (i = 0; i < BUDDY_NUM_ORDERS; i++) {
        list_init(&s_ctx.free_areas[i].free_list);
        s_ctx.free_areas[i].num_free = 0;
    }

    s_init_pfn_cursor = INIT_PFN_NONE;
    boot_alloc_for_each_range(buddy_init_ram_range);

    // Trailing alignment padding of the last walked range
    if (s_init_pfn_cursor != INIT_PFN_NONE) {
        zero_memmap_entries(
            s_init_pfn_cursor,
            ALIGN_UP(s_init_pfn_cursor, BUDDY_WINDOW_PAGES)
        );
    }

    buddy_dump_info();
}
