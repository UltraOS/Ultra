#define MSG_FMT(x) "memory: " x

#include <common/types.h>
#include <common/align.h>
#include <common/format.h>
#include <common/minmax.h>

#include <arch/memory.h>

#include <boot/boot.h>
#include <boot/alloc.h>
#include <private/buddy.h>
#include <private/memory.h>
#include <memory/units.h>
#include <memory/page_table.h>
#include <memory/address_space.h>
#include <memory/page.h>
#include <memory/buddy.h>
#include <memory/io.h>
#include <memory/valloc.h>

#include <init_level.h>
#include <free_after_init.h>
#include <log.h>

static phys_addr_t s_max_ram_addr;

static void INIT_CODE do_for_each_memory_map_range(
    memory_map_range_cb_t mem_cb, memory_filter_cb_t filter_cb, void *user,
    bool erase_ram_type
)
{
    struct ultra_memory_map_attribute *mm = g_boot_ctx.memory_map;
    struct ultra_memory_map_entry *mme, *next_me;
    size_t num_entries, i = 0;
    u32 type;

    num_entries = ULTRA_MEMORY_MAP_ENTRY_COUNT(mm->header);

    while (i < num_entries) {
        phys_addr_t p_start, p_end;
        bool is_ram;

        mme = &mm->entries[i];

        if (!filter_cb(mme)) {
            i++;
            continue;
        }

        is_ram = ultra_mme_is_ram(mme);
        p_start = mme->physical_address;
        p_end = mme->physical_address + mme->size;
        i++;

        /*
         * Only RAM ranges are ever merged. Merging a non-ram range with
         * anything would lose its type, and the merged result would take
         * the wrong truncation path below.
         */
        while (is_ram && i < num_entries) {
            next_me = &mm->entries[i];

            if (!ultra_mme_is_ram(next_me))
                break;

            // We were explicitly forbidden to erase the ram type
            if ((mme->type != next_me->type) && !erase_ram_type)
                break;

            if (p_end != next_me->physical_address || !filter_cb(next_me))
                break;

            p_end += next_me->size;
            i++;
        }

        type = mme->type;

        if (is_ram) {
            if (erase_ram_type)
                type = ULTRA_MEMORY_TYPE_FREE;

            if (p_end > s_max_ram_addr)
                s_max_ram_addr = p_end;

            // Always truncate RAM ranges to the maximum physical address
            p_end = MIN(p_end, MAX_PHYS_ADDR);
        } else if (unlikely(p_end > MAX_PHYS_ADDR)) {
            /*
             * Truncating a non-ram range would hand out a partial mapping
             * of something a caller is trying to locate, so skip it. Loud
             * because no sane firmware produces this.
             */
            pr_warn(
                "skipping out of bounds range 0x%llx-0x%llx (type %u)\n",
                p_start, p_end, type
            );
            continue;
        }

        // p_end might've been truncated below p_start
        if (likely(p_start < p_end))
            mem_cb(p_start, p_end, type, user);
    }
}

void INIT_CODE for_each_memory_map_range(
    memory_map_range_cb_t mem_cb, memory_filter_cb_t filter_cb, void *user
)
{
    do_for_each_memory_map_range(mem_cb, filter_cb, user, false);
}

struct for_each_ram_range_ctx {
    memory_range_cb_t mem_cb;
    void *user;
};

static void INIT_CODE propagate_ram_range(
    phys_addr_t start, phys_addr_t end, u32 type, void *user
)
{
    struct for_each_ram_range_ctx *ctx = user;

    // Type will always be ULTRA_MEMORY_TYPE_FREE here
    UNREFERENCED_PARAMETER(type);

    ctx->mem_cb(start, end, ctx->user);
}

/*
 * This is factored out of for_each_ram_range to allow internal callers that
 * might need additional filtering on top of ultra_mme_is_ram, like the direct
 * map builder.
 */
static void INIT_CODE do_for_each_ram_range(
    memory_range_cb_t mem_cb, memory_filter_cb_t filter_cb, void *user
)
{
    struct for_each_ram_range_ctx ctx = {
        .mem_cb = mem_cb,
        .user = user,
    };

    do_for_each_memory_map_range(
        propagate_ram_range, filter_cb, &ctx, true
    );
}

void INIT_CODE for_each_ram_range(memory_range_cb_t mem_cb, void *user)
{
    do_for_each_ram_range(mem_cb, ultra_mme_is_ram, user);
}

struct address_space g_kernel_address_space;

struct map_range {
    phys_addr_t start, end;
    u8 size_idx;
};

struct direct_mapping_ctx {
    struct pt5 *pt;
    virt_addr_t virt_base;

    ptr_t available_page_sizes[5];
    u8 num_page_sizes;

    /*
     * 2N - 1 is the maximum number of ranges we can get after a split
     * if all N levels are supported as leaves.
     */
    struct map_range ram_ranges[5 * 2 - 1];
    u8 nr_ranges;
};

static void* INIT_CODE pt_early_page_alloc(void)
{
    return boot_alloc_zeroed_or_die(1, "early kernel page tables");
}

static void INIT_CODE detect_page_sizes(struct direct_mapping_ctx *ctx)
{
    size_t i;

    ctx->available_page_sizes[ctx->num_page_sizes++] = PT1_SIZE;

    if (!pt2_can_be_leaf())
        goto out;
    ctx->available_page_sizes[ctx->num_page_sizes++] = PT2_SIZE;

    if (!pt3_can_be_leaf())
        goto out;
    ctx->available_page_sizes[ctx->num_page_sizes++] = PT3_SIZE;

    if (!pt4_can_be_leaf())
        goto out;
    ctx->available_page_sizes[ctx->num_page_sizes++] = PT4_SIZE;

    if (pt5_can_be_leaf())
        ctx->available_page_sizes[ctx->num_page_sizes++] = PT5_SIZE;

out:
    pr_info("supported page sizes:");
    for (i = 0; i < ctx->num_page_sizes; i++) {
        struct human_size hs;

        size_to_human_short(ctx->available_page_sizes[i], &hs);
        pr_cont("%s%zu%c", i == 0 ? " " : ", ", hs.value, *hs.unit);
    }
    pr_cont("\n");
}

static void INIT_CODE split_ram_range(
    struct direct_mapping_ctx *ctx, phys_addr_t start, phys_addr_t end
)
{
    phys_addr_t cur;

    /*
     * RAM ranges are supposed to be aligned to page size, but just to be on
     * the safe side align them here explicitly, so we are able to make
     * progress in the loop below.
     */
    start = ALIGN_UP(start, ctx->available_page_sizes[0]);
    end = ALIGN_DOWN(end, ctx->available_page_sizes[0]);

    ctx->nr_ranges = 0;
    cur = start;

    while (cur < end) {
        u8 i, next_best_idx, best_idx = 0;
        ptr_t this_page_size, best_page_size;
        phys_addr_t current_fit_limit, boundary, next_large_boundary, run_end;

        /*
         * Find the absolute largest page size we can use at 'cur'.
         * Must be perfectly aligned, and must fit within the remaining 'end'.
         *
         * Make sure it's also aligned to our direct map base, which may be
         * half fake when mapping the kernel binary, and thus not suitable
         * for large pages.
         */
        for (i = ctx->num_page_sizes; i-- > 0; ) {
            this_page_size = ctx->available_page_sizes[i];

            if (IS_ALIGNED(cur, this_page_size) &&
                IS_ALIGNED(ctx->virt_base, this_page_size) &&
                (end - cur >= this_page_size)) {
                best_idx = i;
                break;
            }
        }

        best_page_size = ctx->available_page_sizes[best_idx];

        /*
         * Determine how far we can span this chosen page size.
         * We MUST stop if we get too close to the end and 'best_page_size'
         * no longer fits cleanly.
         */
        current_fit_limit = ALIGN_DOWN(end, best_page_size);

        /*
         * We MUST also stop if a LARGER page size suddenly becomes valid.
         * However, we only care about this boundary if the larger page
         * will actually fit in the remaining space before 'end'.
         */
        next_large_boundary = end;
        for (next_best_idx = best_idx + 1;
             next_best_idx < ctx->num_page_sizes; next_best_idx++) {
            this_page_size = ctx->available_page_sizes[next_best_idx];

            if (!IS_ALIGNED(ctx->virt_base, this_page_size))
                break;

            boundary = ALIGN_DOWN(cur, this_page_size) + this_page_size;
            if (boundary < end && (end - boundary >= this_page_size))
                next_large_boundary = MIN(next_large_boundary, boundary);
        }

        run_end = MIN(current_fit_limit, next_large_boundary);
        // Ensure we made at least some progress
        BUG_ON(run_end <= cur);

        /*
         * Save this range. It should be impossible to overflow the
         * ranges array here since it covers the worst case for a 5
         * level page table where all levels can be leaves, but add
         * a check anyway just in case we corrupt it somehow.
         */
        BUG_ON(ctx->nr_ranges >= ARRAY_SIZE(ctx->ram_ranges));
        ctx->ram_ranges[ctx->nr_ranges].start = cur;
        ctx->ram_ranges[ctx->nr_ranges].end = run_end;
        ctx->ram_ranges[ctx->nr_ranges].size_idx = best_idx;
        ctx->nr_ranges++;

        cur = run_end;
    }
}

static bool INIT_CODE ultra_mme_is_ram_except_kernel_binary(
    struct ultra_memory_map_entry *mme
)
{
    /*
     * The kernel binary is excluded on purpose so we don't have r/w mappings
     * for it and can't accidentally corrupt it.
     */
    return mme->type != ULTRA_MEMORY_TYPE_KERNEL_BINARY &&
           ultra_mme_is_ram(mme);
}

static bool INIT_CODE is_leaf_level(
    struct direct_mapping_ctx *ctx, struct map_range *mr, size_t size
)
{
    return ctx->available_page_sizes[mr->size_idx] == size;
}

static void INIT_CODE direct_map_pt1(
    struct pt1 *pt1, phys_addr_t phys, phys_addr_t end,
    enum vm_prot prot
)
{
    phys_addr_t next;

    for (; phys < end; phys = next, pt1++) {
        next = ALIGN_DOWN(phys, PT1_SIZE) + PT1_SIZE;
        if (next < phys || next > end)
            next = end;

        pt1_exclusive_make_leaf(pt1, phys, pt_prot_from_vm_prot(prot));
    }
}

static void INIT_CODE direct_map_pt2(
    struct direct_mapping_ctx *ctx, struct map_range *mr,
    struct pt2 *pt2, phys_addr_t phys, phys_addr_t end,
    enum vm_prot prot
)
{
    virt_addr_t virt, next_virt;
    phys_addr_t next_phys;
    struct pt1 *pt1;

    for (virt = ctx->virt_base + phys; phys < end;
         phys = next_phys, virt = next_virt, pt2++) {
        next_virt = ALIGN_DOWN(virt, PT2_SIZE) + PT2_SIZE;
        next_phys = next_virt - ctx->virt_base;

        if (next_phys < phys || next_phys > end)
            next_phys = end;

        if (is_leaf_level(ctx, mr, PT2_SIZE)) {
            pt2_exclusive_make_leaf(pt2, phys, pt_prot_from_vm_prot(prot));
        } else {
            if (!pt2_present(pt2))
                pt2_exclusive_populate(pt2, pt_early_page_alloc());

            pt1 = pt1_from_pt2(pt2, virt);
            direct_map_pt1(pt1, phys, next_phys, prot);
        }
    }
}

static void INIT_CODE direct_map_pt3(
    struct direct_mapping_ctx *ctx, struct map_range *mr,
    struct pt3 *pt3, phys_addr_t phys, phys_addr_t end,
    enum vm_prot prot
)
{
    virt_addr_t virt, next_virt;
    phys_addr_t next_phys;
    struct pt2 *pt2;

    for (virt = ctx->virt_base + phys; phys < end;
         phys = next_phys, virt = next_virt, pt3++) {
        next_virt = ALIGN_DOWN(virt, PT3_SIZE) + PT3_SIZE;
        next_phys = next_virt - ctx->virt_base;

        if (next_phys < phys || next_phys > end)
            next_phys = end;

        if (is_leaf_level(ctx, mr, PT3_SIZE)) {
            pt3_exclusive_make_leaf(pt3, phys, pt_prot_from_vm_prot(prot));
        } else {
            if (!pt3_present(pt3))
                pt3_exclusive_populate(pt3, pt_early_page_alloc());

            pt2 = pt2_from_pt3(pt3, virt);
            direct_map_pt2(ctx, mr, pt2, phys, next_phys, prot);
        }
    }
}

static void INIT_CODE direct_map_pt4(
    struct direct_mapping_ctx *ctx, struct map_range *mr,
    struct pt4 *pt4, phys_addr_t phys, phys_addr_t end,
    enum vm_prot prot
)
{
    virt_addr_t virt, next_virt;
    phys_addr_t next_phys;
    struct pt3 *pt3;

    for (virt = ctx->virt_base + phys; phys < end;
         phys = next_phys, virt = next_virt, pt4++) {
        next_virt = ALIGN_DOWN(virt, PT4_SIZE) + PT4_SIZE;
        next_phys = next_virt - ctx->virt_base;

        if (next_phys < phys || next_phys > end)
            next_phys = end;

        if (is_leaf_level(ctx, mr, PT4_SIZE)) {
            pt4_exclusive_make_leaf(pt4, phys, pt_prot_from_vm_prot(prot));
        } else {
            if (!pt4_present(pt4))
                pt4_exclusive_populate(pt4, pt_early_page_alloc());

            pt3 = pt3_from_pt4(pt4, virt);
            direct_map_pt3(ctx, mr, pt3, phys, next_phys, prot);
        }
    }
}

static void INIT_CODE direct_map_pt5(
    struct direct_mapping_ctx *ctx, struct map_range *mr,
    enum vm_prot prot
)
{
    struct pt5 *pt5;
    struct pt4 *pt4;
    phys_addr_t next_phys;
    virt_addr_t virt, next_virt;
    phys_addr_t phys = mr->start, end = mr->end;

    virt = ctx->virt_base + phys;
    pt5 = pt5_from_pt5_base(ctx->pt, virt);

    for (; phys < end; phys = next_phys, virt = next_virt, pt5++) {
        next_virt = ALIGN_DOWN(virt, PT5_SIZE) + PT5_SIZE;
        next_phys = next_virt - ctx->virt_base;

        if (next_phys < phys || next_phys > end)
            next_phys = end;

        if (is_leaf_level(ctx, mr, PT5_SIZE)) {
            pt5_exclusive_make_leaf(pt5, phys, pt_prot_from_vm_prot(prot));
        } else {
            if (!pt5_present(pt5))
                pt5_exclusive_populate(pt5, pt_early_page_alloc());

            pt4 = pt4_from_pt5(pt5, virt);
            direct_map_pt4(ctx, mr, pt4, phys, next_phys, prot);
        }
    }
}

static void INIT_CODE split_and_map_range(
    struct direct_mapping_ctx *ctx, phys_addr_t start, phys_addr_t end,
    enum vm_prot prot
)
{
    struct map_range *mr;
    size_t i, page_size;
    struct human_size hs;

    split_ram_range(ctx, start, end);

    for (i = 0; i < ctx->nr_ranges; i++) {
        mr = &ctx->ram_ranges[i];

        page_size = ctx->available_page_sizes[mr->size_idx];
        size_to_human_short(page_size, &hs);

        pr_debug(
            "  [0x%016llX - 0x%016llX] %zu%c pages\n",
            mr->start, mr->end, hs.value, *hs.unit
        );

        direct_map_pt5(ctx, mr, prot);
    }
}

static void INIT_CODE direct_map_one(
    phys_addr_t start, phys_addr_t end, void *ctx
)
{
    pr_debug(
        "direct mapping [0x%016llX - 0x%016llX] as:\n",
        start, end
    );
    split_and_map_range(
        ctx, start, end, VM_PROT_KERNEL | VM_PROT_READ | VM_PROT_WRITE
    );
}

extern u8 LINKER_SYMBOL(executable_data_begin)[];
extern u8 LINKER_SYMBOL(executable_data_end)[];

extern u8 LINKER_SYMBOL(readonly_data_begin)[];
extern u8 LINKER_SYMBOL(readonly_data_end)[];

extern u8 LINKER_SYMBOL(readwrite_data_begin)[];
extern u8 LINKER_SYMBOL(readwrite_data_end)[];

static void INIT_CODE map_kernel_segment(
    struct direct_mapping_ctx *ctx, struct ultra_kernel_info_attribute *ki,
    u8 *begin_sym, u8 *end_sym, const char *name, enum vm_prot prot
)
{
    virt_addr_t v_start, v_end;
    phys_addr_t p_start, p_end;

    v_start = (virt_addr_t)begin_sym;
    v_end = (virt_addr_t)end_sym;
    pr_debug(
        "mapping kernel %s [0x%016zX - 0x%016zX] as:\n", name,
        v_start, v_end
    );

    BUG_ON(
        v_start >= v_end ||
        !IS_PAGE_ALIGNED(v_start) ||
        !IS_PAGE_ALIGNED(v_end)
    );

    p_start = (v_start - ki->virtual_base) + ki->physical_base;
    p_end = (v_end - ki->virtual_base) + ki->physical_base;

    split_and_map_range(ctx, p_start, p_end, prot);
}

static void INIT_CODE build_kernel_mappings(struct direct_mapping_ctx *ctx)
{
    struct ultra_kernel_info_attribute *ki = g_boot_ctx.kernel_info;

    /*
     * Subtract here because the mapper assumes a direct map, thus we need to
     * offset the base by its physical address.
     */
    ctx->virt_base = ki->virtual_base - ki->physical_base;

    map_kernel_segment(
        ctx, ki,
        LINKER_SYMBOL(executable_data_begin),
        LINKER_SYMBOL(executable_data_end),
        "executable data",
        VM_PROT_KERNEL | VM_PROT_READ | VM_PROT_EXEC
    );

    map_kernel_segment(
        ctx, ki,
        LINKER_SYMBOL(readonly_data_begin),
        LINKER_SYMBOL(readonly_data_end),
        "read-only data",
        VM_PROT_KERNEL | VM_PROT_READ
    );

    map_kernel_segment(
        ctx, ki,
        LINKER_SYMBOL(readwrite_data_begin),
        LINKER_SYMBOL(readwrite_data_end),
        "read-write data",
        VM_PROT_KERNEL | VM_PROT_READ | VM_PROT_WRITE
    );
}

static error_t INIT_CODE kernel_address_space_setup(void)
{
    struct direct_mapping_ctx ctx = { 0 };

    ctx.pt = pt_early_page_alloc();

    detect_page_sizes(&ctx);

    build_kernel_mappings(&ctx);

    /*
     * Only RAM is mapped in the kernel direct map.
     * MMIO and other memory uses io_window_map with proper caching
     * attributes. The direct map is always write-back cached.
     */
    ctx.virt_base = g_direct_map_base;
    do_for_each_ram_range(
        direct_map_one, ultra_mme_is_ram_except_kernel_binary, &ctx
    );

    g_kernel_address_space.pt = ctx.pt;
    pr_lvl(
        s_max_ram_addr <= MAX_PHYS_ADDR ? LOG_LEVEL_INFO : LOG_LEVEL_WARN,
        "max RAM address: %llX, max supported: %llX",
        s_max_ram_addr, MAX_PHYS_ADDR
    );
    return EOK;
}
INIT_CALL_AT(KERNEL_ADDRESS_SPACE_AVAILABLE, kernel_address_space_setup);

static pt_prot s_kernel_memory_map_pt_prot;

static void INIT_CODE memory_map_populate_pt1(
    struct pt1 *pt1, virt_addr_t virt, virt_addr_t end
)
{
    virt_addr_t next;
    phys_addr_t pa;

    for (; virt < end; virt = next, pt1++) {
        next = ALIGN_DOWN(virt, PT1_SIZE) + PT1_SIZE;
        if (next < virt || next > end)
            next = end;

        if (!pt1_present(pt1)) {
            pa = virt_to_phys(boot_alloc_or_die(1, "kernel memory map"));
            pt1_exclusive_make_leaf(pt1, pa, s_kernel_memory_map_pt_prot);
        }
    }
}

static void INIT_CODE memory_map_populate_pt2(
    struct pt2 *pt2, virt_addr_t virt, virt_addr_t end
)
{
    virt_addr_t next;
    struct pt1 *pt1;
    phys_addr_t huge_pt;

    for (; virt < end; virt = next, pt2++) {
        next = ALIGN_DOWN(virt, PT2_SIZE) + PT2_SIZE;
        if (next < virt || next > end)
            next = end;

        if (!pt2_can_be_leaf())
            goto do_small_pages;
        if (!IS_ALIGNED(virt, PT2_SIZE) || (next - virt) != PT2_SIZE)
            goto do_small_pages;

        if (pt2_present(pt2)) {
            if (pt2_is_leaf(pt2))
                continue;

            goto do_small_pages;
        }

        huge_pt = boot_alloc_aligned(PT2_SIZE / PAGE_SIZE, PT2_SIZE);
        if (error_phys_addr(huge_pt))
            goto do_small_pages;

        pt2_exclusive_make_leaf(pt2, huge_pt, s_kernel_memory_map_pt_prot);
        continue;

    do_small_pages:
        if (!pt2_present(pt2))
            pt2_exclusive_populate(pt2, pt_early_page_alloc());

        pt1 = pt1_from_pt2(pt2, virt);
        memory_map_populate_pt1(pt1, virt, next);
    }
}

static void INIT_CODE memory_map_populate_pt3(
    struct pt3 *pt3, virt_addr_t virt, virt_addr_t end
)
{
    virt_addr_t next;
    struct pt2 *pt2;

    for (; virt < end; virt = next, pt3++) {
        next = ALIGN_DOWN(virt, PT3_SIZE) + PT3_SIZE;
        if (next < virt || next > end)
            next = end;

        if (!pt3_present(pt3))
            pt3_exclusive_populate(pt3, pt_early_page_alloc());

        pt2 = pt2_from_pt3(pt3, virt);
        memory_map_populate_pt2(pt2, virt, next);
    }
}

static void INIT_CODE memory_map_populate_pt4(
    struct pt4 *pt4, virt_addr_t virt, virt_addr_t end
)
{
    virt_addr_t next;
    struct pt3 *pt3;

    for (; virt < end; virt = next, pt4++) {
        next = ALIGN_DOWN(virt, PT4_SIZE) + PT4_SIZE;
        if (next < virt || next > end)
            next = end;

        if (!pt4_present(pt4))
            pt4_exclusive_populate(pt4, pt_early_page_alloc());

        pt3 = pt3_from_pt4(pt4, virt);
        memory_map_populate_pt3(pt3, virt, next);
    }
}

static void INIT_CODE memory_map_populate_pt5(
    virt_addr_t virt, virt_addr_t end
)
{
    virt_addr_t next;
    struct pt5 *pt5;
    struct pt4 *pt4;

    pt5 = pt_root_from_address_space(&g_kernel_address_space, virt);

    for (; virt < end; virt = next, pt5++) {
        next = ALIGN_DOWN(virt, PT5_SIZE) + PT5_SIZE;
        if (next < virt || next > end)
            next = end;

        if (!pt5_present(pt5))
            pt5_exclusive_populate(pt5, pt_early_page_alloc());

        pt4 = pt4_from_pt5(pt5, virt);
        memory_map_populate_pt4(pt4, virt, next);
    }
}

static inline virt_addr_t phys_to_memory_map(phys_addr_t phys)
{
    return MEMORY_MAP_BASE + ((phys / PAGE_SIZE) * sizeof(struct page));
}

static void INIT_CODE kernel_memory_setup_one(
    phys_addr_t p_start, phys_addr_t p_end, void *unused
)
{
    virt_addr_t v_start, v_end;

    UNREFERENCED_PARAMETER(unused);

    p_start = ALIGN_DOWN(p_start, BUDDY_MAX_SIZE);
    p_end = ALIGN_UP(p_end, BUDDY_MAX_SIZE);

    v_start = phys_to_memory_map(p_start);
    v_end = phys_to_memory_map(p_end);

    v_start = ALIGN_DOWN(v_start, PAGE_SIZE);
    v_end = ALIGN_UP(v_end, PAGE_SIZE);

    pr_debug(
        "populating memmap [0x%016zX/%0llX - 0x%016zX/%0llX]\n",
        v_start, p_start, v_end, p_end
    );

    memory_map_populate_pt5(v_start, v_end);
}

struct page *g_memory_map;

static error_t INIT_CODE kernel_memory_map_setup(void)
{
    s_kernel_memory_map_pt_prot = pt_prot_from_vm_prot(
        VM_PROT_KERNEL | VM_PROT_READ | VM_PROT_WRITE
    );

    for_each_ram_range(kernel_memory_setup_one, nullptr);
    g_memory_map = (struct page*)MEMORY_MAP_BASE;
    return EOK;
}
INIT_CALL_AT(MEMORY_MAP_AVAILABLE, kernel_memory_map_setup);

/*
 * Register all permanent virtual areas of the kernel half of the address
 * space. This is mostly a safety measure so no other caller of the vreserve()
 * family of APIs is able to overwrite actually in-use kernel regions.
 *
 * As a bonus we get a nice way to dump the entire kernel address space.
 */
static error_t INIT_CODE register_permanent_areas(void)
{
    error_t ret, out_ret = EOK;

    ret = vreserve_permanent(MEMORY_MAP_BASE, MEMORY_MAP_END, "memory map");
    if (is_error(ret))
        out_ret = ret;

    ret = vreserve_permanent(
        g_boot_ctx.kernel_info->virtual_base,
        g_boot_ctx.kernel_info->virtual_base + g_boot_ctx.kernel_info->size,
        "kernel binary"
    );
    if (is_error(ret))
        out_ret = ret;

    ret = vreserve_permanent(
        g_direct_map_base,
        g_direct_map_base + MAX_PHYS_ADDR,
        "direct map"
    );
    if (is_error(ret))
        out_ret = ret;

    /*
     * VALLOC_BASE -> VALLOC_END isn't registered here because it's
     * not a preallocated region, but rather the "bounds" that valloc()
     * does its allocations within to keep them organized.
     */

    if (is_error(out_ret))
        pr_warn("failed to reserve one or more permanent areas!\n");
    return out_ret;
}
INIT_CALL_PRE(VALLOC_AVAILABLE, register_permanent_areas);
