#include <common/format.h>
#include <common/helpers.h>
#include <common/minmax.h>

#include <memory/alloc_cache.h>
#include <memory/buddy.h>
#include <memory/describe.h>
#include <memory/io.h>
#include <memory/page.h>

#include <private/per_cpu.h>
#include <private/symbols.h>

#include <init_level.h>
#include <unsafe_access.h>

static const char *const s_page_type_names[] = {
    [PAGE_TYPE_RESERVED] = "reserved",
    [PAGE_TYPE_BUDDY] = "free",
    [PAGE_TYPE_GENERIC_FROZEN] = "frozen",
    [PAGE_TYPE_KHEAP] = "kheap",
    [PAGE_TYPE_KHEAP_LARGE] = "kheap large",
    [PAGE_TYPE_PTDESC] = "page table",
    [PAGE_TYPE_VALLOC_MANAGED] = "valloc",
    [PAGE_TYPE_GENERIC] = "generic",
    [PAGE_TYPE_ANON] = "anon",
    [PAGE_TYPE_PAGE_CACHE] = "page cache",
};

static const char *page_type_name(enum page_type type)
{
    if (unlikely(type >= ARRAY_SIZE(s_page_type_names) ||
                 s_page_type_names[type] == nullptr))
        return "unknown";

    return s_page_type_names[type];
}

// A pfn or a tail link can point anywhere, check before reading
static bool address_is_struct_page(ptr_t address)
{
    return within_bounds(address, MEMORY_MAP_BASE, MEMORY_MAP_END) &&
           (address - MEMORY_MAP_BASE) % sizeof(struct page) == 0;
}

/*
 * Read the meta of 'page', or of its block's page 0 if it is a tail,
 * without faulting on a bad pointer. False if either is unreadable.
 */
static bool read_block_meta(
    const struct page *page, const struct page **out_block, ptr_t *out_meta
)
{
    ptr_t meta;

    if (!address_is_struct_page((ptr_t)page))
        return false;
    if (try_memcpy(&meta, &page->meta, sizeof(meta)) != EOK)
        return false;

    if (META_IS_TAIL(meta)) {
        if (!address_is_struct_page(META_TO_BLOCK_PTR(meta)))
            return false;

        page = block_to_page(META_TO_BLOCK(meta));
        if (try_memcpy(&meta, &page->meta, sizeof(meta)) != EOK)
            return false;
    }

    *out_block = page;
    *out_meta = meta;
    return true;
}

static struct string describe_struct_page(
    const void *addr, char out_desc_buf[static MAX_MEM_DESCRIPTION_LENGTH]
)
{
    const struct page *page, *block;
    size_t offset;
    ptr_t meta;

    offset = ((ptr_t)addr - MEMORY_MAP_BASE) % sizeof(struct page);
    page = addr - offset;

    if (!read_block_meta(page, &block, &meta)) {
        return str_printf(
            out_desc_buf, MAX_MEM_DESCRIPTION_LENGTH,
            "unreadable struct page of pfn 0x%zx +0x%zx", page_to_pfn(page),
            offset
        );
    }

    return str_printf(
        out_desc_buf, MAX_MEM_DESCRIPTION_LENGTH,
        "struct page of pfn 0x%zx +0x%zx, %s block pfn 0x%zx order %hhu",
        page_to_pfn(page), offset, page_type_name(META_TO_TYPE(meta)),
        page_to_pfn(block), META_TO_ORDER(meta)
    );
}

static struct string page_describe(
    const void *addr, char out_desc_buf[static MAX_MEM_DESCRIPTION_LENGTH]
)
{
    const struct page *block;
    phys_addr_t phys;
    ptr_t meta;

    if (init_level_below(INIT_LEVEL_MEMORY_MAP_AVAILABLE))
        return NULL_STR();

    if (within_bounds((ptr_t)addr, MEMORY_MAP_BASE, MEMORY_MAP_END))
        return describe_struct_page(addr, out_desc_buf);

    if (!pointer_is_direct_map(addr))
        return NULL_STR();

    phys = virt_to_phys(addr);
    if (!read_block_meta(phys_to_page(phys), &block, &meta)) {
        return str_printf(
            out_desc_buf, MAX_MEM_DESCRIPTION_LENGTH,
            "direct map %pPA without a page", &phys
        );
    }

    return str_printf(
        out_desc_buf, MAX_MEM_DESCRIPTION_LENGTH,
        "%s block pfn 0x%zx order %hhu +0x%zx",
        page_type_name(META_TO_TYPE(meta)), page_to_pfn(block),
        META_TO_ORDER(meta), (size_t)(phys - page_to_phys(block))
    );
}

static struct string describe_slab(
    const struct page *block, ptr_t meta, size_t offset,
    char out_desc_buf[static MAX_MEM_DESCRIPTION_LENGTH]
)
{
    struct alloc_cache cache, *cache_ptr;
    char name[32] = { 0 };
    size_t slab_size, index;

    // The slab can be freed and reused meanwhile, so trust nothing in it
    if (try_memcpy(&cache_ptr, &block->kheap.cache, sizeof(cache_ptr)) != EOK)
        goto out_unreadable;
    if (try_memcpy(&cache, cache_ptr, sizeof(cache)) != EOK)
        goto out_unreadable;
    if (try_memcpy(name, cache.name, sizeof(name) - 1) != EOK)
        goto out_unreadable;
    if (cache.object_size == 0)
        goto out_unreadable;

    slab_size = order_to_bytes(META_TO_ORDER(meta));
    index = offset / cache.object_size;

    // The leftover bytes at the end of a slab that fit no object
    if (index >= slab_size / cache.object_size) {
        return str_printf(
            out_desc_buf, MAX_MEM_DESCRIPTION_LENGTH,
            "kheap %s slab pfn 0x%zx tail +0x%zx", name, page_to_pfn(block),
            offset
        );
    }

    return str_printf(
        out_desc_buf, MAX_MEM_DESCRIPTION_LENGTH, "kheap %s object %p +0x%zx",
        name, (char*)page_to_virt(block) + index * cache.object_size,
        offset % cache.object_size
    );

out_unreadable:
    return str_printf(
        out_desc_buf, MAX_MEM_DESCRIPTION_LENGTH,
        "kheap slab pfn 0x%zx +0x%zx, unreadable cache", page_to_pfn(block),
        offset
    );
}

static struct string kheap_describe(
    const void *addr, char out_desc_buf[static MAX_MEM_DESCRIPTION_LENGTH]
)
{
    const struct page *block;
    phys_addr_t phys;
    size_t offset;
    ptr_t meta;

    if (init_level_below(INIT_LEVEL_HEAP_AVAILABLE))
        return NULL_STR();
    if (!pointer_is_direct_map(addr))
        return NULL_STR();

    phys = virt_to_phys(addr);
    if (!read_block_meta(phys_to_page(phys), &block, &meta))
        return NULL_STR();

    offset = phys - page_to_phys(block);

    switch (META_TO_TYPE(meta)) {
    case PAGE_TYPE_KHEAP:
        return describe_slab(block, meta, offset, out_desc_buf);
    case PAGE_TYPE_KHEAP_LARGE:
        return str_printf(
            out_desc_buf, MAX_MEM_DESCRIPTION_LENGTH,
            "kheap large allocation %p order %hhu +0x%zx",
            page_to_virt(block), META_TO_ORDER(meta), offset
        );
    default:
        return NULL_STR();
    }
}

typedef struct string (*describer_t)(
    const void *addr, char out_desc_buf[static MAX_MEM_DESCRIPTION_LENGTH]
);

/*
 * Each describer handles one kind of memory and returns NULL_STR() for an
 * address that isn't its own. A report can come from any context with any
 * address, so a describer only takes relaxed reads and copies through
 * try_memcpy().
 */
static const describer_t s_describers[] = {
    symbol_describe,
    per_cpu_describe,
    kheap_describe,
    page_describe,
};

struct string mem_describe(
    const void *addr, char out_desc_buf[static MAX_MEM_DESCRIPTION_LENGTH]
)
{
    struct string desc;
    size_t i;

    for (i = 0; i < ARRAY_SIZE(s_describers); i++) {
        desc = s_describers[i](addr, out_desc_buf);
        if (!str_is_null(desc))
            return desc;
    }

    return str_printf(
        out_desc_buf, MAX_MEM_DESCRIPTION_LENGTH, "unknown memory"
    );
}
