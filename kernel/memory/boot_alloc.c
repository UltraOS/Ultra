#define MSG_FMT(msg) "boot-alloc: " msg

#include <common/types.h>
#include <common/string.h>
#include <common/minmax.h>
#include <common/align.h>

#include <boot/boot.h>
#include <boot/ultra_protocol.h>
#include <boot/alloc.h>

#include <log.h>
#include <bug.h>
#include <io.h>

struct memory_range {
    phys_addr_t physical_address;

#define RANGE_TYPE_MASK 0b1ull
#define MEMORY_FREE 0
#define MEMORY_ALLOCATED 1
    phys_addr_t size_and_type;
};

#define MR_ENCODE(size, type) ((size) | (type))
#define MR_TYPE(range) ((range)->size_and_type & RANGE_TYPE_MASK)
#define MR_SIZE(range) ((range)->size_and_type & ~RANGE_TYPE_MASK)

static inline phys_addr_t mr_end(struct memory_range *mr)
{
    return mr->physical_address + MR_SIZE(mr);
}

#define BOOT_ALLOC_INITIAL_CAPACITY (PAGE_SIZE / sizeof(struct memory_range))
static struct memory_range g_initial_buffer[BOOT_ALLOC_INITIAL_CAPACITY];

static struct memory_range *g_buffer = g_initial_buffer;
static size_t g_capacity = BOOT_ALLOC_INITIAL_CAPACITY;
static size_t g_entry_count = 0;

static void range_insert(
    struct memory_range *mr, size_t idx, size_t count
)
{
    size_t bytes_to_move;
    BUG_ON(idx > count);

    if (idx == count)
        goto range_place;

    bytes_to_move = (count - idx) * sizeof(*mr);
    memmove(&g_buffer[idx + 1], &g_buffer[idx], bytes_to_move);

range_place:
    g_buffer[idx] = *mr;
}

static void range_emplace_at(size_t idx, struct memory_range *mr)
{
    BUG_ON(idx > g_entry_count);
    BUG_ON(g_entry_count >= g_capacity);

    range_insert(mr, idx, g_entry_count++);
}

enum allow_one_above {
    ALLOW_ONE_ABOVE_NO = 0,
    ALLOW_ONE_ABOVE_YES,
};

ssize_t find_range(phys_addr_t value, enum allow_one_above allow_above)
{
    ssize_t left = 0;
    ssize_t right = g_entry_count - 1;

    while (left <= right) {
        ssize_t middle = left + ((right - left) / 2);

        if (g_buffer[middle].physical_address < value) {
            left = middle + 1;
        } else if (value < g_buffer[middle].physical_address) {
            right = middle - 1;
        } else {
            return middle;
        }
    }

    // Left is always lower bound, right is always lower bound - 1
    if (right >= 0) {
        struct memory_range *mr = &g_buffer[right];

        if (mr->physical_address < value && value < mr_end(mr))
            return right;
    }

    // Don't return out of bounds range, even if it's lower bound
    if (left == (ssize_t)g_entry_count)
        left = -1;

    // Either return the lower bound range (aka one after "value") or none
    return allow_above == ALLOW_ONE_ABOVE_YES ? left : -1;
}

static bool can_merge_ranges(struct memory_range *lhs, struct memory_range *rhs)
{
    return MR_TYPE(lhs) == MR_TYPE(rhs) &&
           mr_end(lhs) == rhs->physical_address;
}

static void merge_ranges(struct memory_range *dst, struct memory_range *src)
{
    // Only a backward merge requires an address change
    if (dst->physical_address > src->physical_address)
        dst->physical_address = src->physical_address;

    dst->size_and_type += MR_SIZE(src);
}

static struct memory_range *range_before(size_t mr_idx)
{
    return mr_idx ? &g_buffer[mr_idx - 1] : NULL;
}

static struct memory_range *range_after(size_t mr_idx)
{
    if (mr_idx == g_entry_count - 1)
        return NULL;

    return &g_buffer[mr_idx + 1];
}

static void allocate_out_of(size_t mr_idx, struct memory_range *new_mr)
{
    struct memory_range *current_mr = &g_buffer[mr_idx];
    struct memory_range mr_lhs_piece, mr_rhs_piece;
    struct memory_range *mr_before, *mr_after;
    bool mergeable_before, mergeable_after;

    mr_lhs_piece.physical_address = current_mr->physical_address;
    mr_lhs_piece.size_and_type = MR_ENCODE(
        new_mr->physical_address - current_mr->physical_address,
        MR_TYPE(current_mr)
    );

    mr_rhs_piece.physical_address = mr_end(new_mr);
    mr_rhs_piece.size_and_type = MR_ENCODE(
        mr_end(current_mr) - mr_end(new_mr),
        MR_TYPE(current_mr)
    );

    // New map entry is always either fully inside this one or equal to it
    BUG_ON(current_mr->physical_address > new_mr->physical_address ||
           mr_end(current_mr) < mr_end(new_mr));
    BUG_ON(MR_TYPE(current_mr) == MR_TYPE(new_mr));

    /*
     * When we allocate a piece of an existing range we have the following
     * cases to account for:
     * 1. The range is allocated in the middle, it still has pieces on the
     *    sides. This is the simplest case, as it's guaranteed that no
     *    surrounding entries are mergeable with this new one. This case
     *    increases the memory map by two entries.
     * 2. The range is allocated on the left, but still has a piece on the
     *    right. This splits into two more cases:
     *    2a. The left piece is mergeable with the range before it. Do the
     *        merge, and leave the range we have allocated from where it was
     *        with its beginning carved out. The memory map size doesn't change.
     *    2b. The left piece is not mergeable with the range before it or
     *        this is the first range. In this case the left piece replaces the
     *        entry where the piece we have allocated from lived, and the
     *        remaining right hand side of it is inserted right after. The
     *        memory map grows by one entry.
     * 3. The range is allocated on the right, but still has a piece on the
     *    left. This is the exact copy of case 2 but inverted.
     * 4. The range is allocated entirely. The following cases exist:
     *    4a. The new allocation is not mergeable with the range before nor the
     *        range after. Simply change the type of the range we have allocated
     *        from. The memory map size doesn't change.
     *    4b. The new range is mergeable with both left and right ranges. Merge
     *        all ranges into the left-most range, and move all ranges after the
     *        ones we have merged back by two. The memory map size drops by 2.
     *    4c. The new range is only mergeable with the left range. Perform the
     *        merge with the left range and move all other ranges back by 1.
     *        The memory map size is reduced by 1.
     *    4d. The new range is only mergeable with the right range. Merge the
     *        new range into the right range and move all the ranges after
     *        back by 1. The memory map size is reduced by 1.
     */

    // Case 1
    if (MR_SIZE(&mr_lhs_piece) != 0 && MR_SIZE(&mr_rhs_piece) != 0) {
        g_buffer[mr_idx++] = mr_lhs_piece;
        range_emplace_at(mr_idx++, new_mr);
        range_emplace_at(mr_idx, &mr_rhs_piece);

        // See two range_emplace_at calls above
        #define BOOT_ALLOC_WORST_CASE_GROWTH_PER_ALLOCATION 2

        return;
    }

    mr_before = range_before(mr_idx);
    mergeable_before = mr_before && can_merge_ranges(mr_before, new_mr);

    // Case 2
    if (!MR_SIZE(&mr_lhs_piece) && MR_SIZE(&mr_rhs_piece)) {
        // Case 2a
        if (mergeable_before) {
            merge_ranges(mr_before, new_mr);
            *current_mr = mr_rhs_piece;
            return;
        }

        // Case 2b
        g_buffer[mr_idx++] = *new_mr;
        range_emplace_at(mr_idx, &mr_rhs_piece);
        return;
    }

    mr_after = range_after(mr_idx);
    mergeable_after = mr_after && can_merge_ranges(new_mr, mr_after);

    // Case 3
    if (MR_SIZE(&mr_lhs_piece) && !MR_SIZE(&mr_rhs_piece)) {
        // Case 3a
        if (mergeable_after) {
            *current_mr = mr_lhs_piece;
            merge_ranges(mr_after, new_mr);
            return;
        }

        // Case 3b
        g_buffer[mr_idx++] = mr_lhs_piece;
        range_emplace_at(mr_idx, new_mr);
        return;
    }

    // Case 4a
    if (!mergeable_before && !mergeable_after) {
        current_mr->size_and_type = new_mr->size_and_type;
        return;
    }

    // Case 4b
    if (mergeable_before && mergeable_after) {
        merge_ranges(mr_before, new_mr);
        merge_ranges(mr_before, mr_after++);
        goto out_case4;
    }

    // Case 4c
    if (mergeable_before) {
        merge_ranges(mr_before, new_mr);
        goto out_case4;
    }

    // Case 4d
    merge_ranges(mr_after, new_mr);

out_case4:
    if (mr_after != NULL) {
        memmove(
            current_mr, mr_after,
            ((g_buffer + g_entry_count) - mr_after) * sizeof(*mr_after)
        );
    }
    g_entry_count -= mergeable_before + mergeable_after;
}

static phys_addr_t allocate_top_down(size_t page_count, phys_addr_t upper_limit)
{
    phys_addr_t range_end, bytes_to_allocate = page_count * PAGE_SIZE;
    phys_addr_t allocated_end = 0;
    struct memory_range allocated_mr;
    size_t i = g_entry_count;

    BUG_ON_WITH_MSG(
        bytes_to_allocate <= page_count,
        "invalid allocation size (%zu pages)\n", page_count
    );

    while (i-- > 0) {
        struct memory_range *mr = &g_buffer[i];

        if (mr->physical_address >= upper_limit)
            continue;

        if (MR_TYPE(mr) != MEMORY_FREE)
            continue;

        range_end = MIN(mr_end(mr), upper_limit);

        // Not enough length after cutoff
        if ((range_end - mr->physical_address) < bytes_to_allocate)
            continue;

        allocated_end = range_end;
        break;
    }

    if (!allocated_end)
        return encode_error_phys_addr(ENOMEM);

    allocated_mr = (struct memory_range) {
        .physical_address = allocated_end - bytes_to_allocate,
        .size_and_type = MR_ENCODE(bytes_to_allocate, MEMORY_ALLOCATED),
    };
    allocate_out_of(i, &allocated_mr);

    return allocated_mr.physical_address;
}

static phys_addr_t allocate_within(
    size_t page_count, phys_addr_t lower_limit, phys_addr_t upper_limit
)
{
    phys_addr_t range_begin;
    size_t bytes_to_allocate;;
    ssize_t mr_idx;
    struct memory_range *picked_mr = NULL;
    struct memory_range allocated_mr;

    bytes_to_allocate = page_count * PAGE_SIZE;
    BUG_ON_WITH_MSG(
        bytes_to_allocate <= page_count,
        "invalid allocation size (%zu pages)\n", page_count
    );

    // invalid input
    if (lower_limit >= upper_limit)
        goto out_invalid_allocation;

    // search gap is too small
    if (lower_limit + bytes_to_allocate > upper_limit)
        goto out_invalid_allocation;

    // overflow
    if (lower_limit + bytes_to_allocate < lower_limit)
        goto out_invalid_allocation;

    mr_idx = find_range(lower_limit, ALLOW_ONE_ABOVE_YES);
    if (mr_idx < 0)
        return encode_error_phys_addr(ENOMEM);

    for (; mr_idx < (ssize_t)g_entry_count; ++mr_idx) {
        phys_addr_t end;
        u64 available_gap;

        picked_mr = &g_buffer[mr_idx];
        end = mr_end(picked_mr);

        if (picked_mr->physical_address > upper_limit)
            return encode_error_phys_addr(ENOMEM);

        if (MR_TYPE(picked_mr) != MEMORY_FREE)
            goto next_range;

        available_gap = MIN(end, upper_limit) -
                        MAX(picked_mr->physical_address, lower_limit);
        if (available_gap >= bytes_to_allocate)
            break;

    next_range:
        if (end >= upper_limit)
            return encode_error_phys_addr(ENOMEM);

        if ((upper_limit - end) < bytes_to_allocate)
            return encode_error_phys_addr(ENOMEM);
    }

    if (mr_idx == (ssize_t)g_entry_count)
        return encode_error_phys_addr(ENOMEM);

    range_begin = MAX(lower_limit, picked_mr->physical_address);
    allocated_mr = (struct memory_range) {
        .physical_address = range_begin,
        .size_and_type = MR_ENCODE(bytes_to_allocate, MEMORY_ALLOCATED),
    };
    allocate_out_of(mr_idx, &allocated_mr);

    return allocated_mr.physical_address;

out_invalid_allocation:
    BUG_WITH_MSG(
        "invalid allocation: %zu pages within 0x%016llX -> 0x%016llX\n",
         page_count, lower_limit, upper_limit
    );
}

static phys_addr_t boot_alloc_nogrow(size_t num_pages)
{
    return allocate_top_down(num_pages, -1ull);
}

static bool maybe_grow_buffer(void)
{
    void *new_buffer;
    phys_addr_t addr;
    size_t growth_watermark, new_capacity;

    /*
     * Base watermark is the capacity for at least two worst-cast allocations:
     * the one we're about to do at the callsite, and the array growth here
     * (that will be invoked at the next allocation).
     */
    growth_watermark = BOOT_ALLOC_WORST_CASE_GROWTH_PER_ALLOCATION * 2;

    /*
     * If the current buffer is a dynamic allocation, we must account for the
     * boot_free() "allocation" as well.
     */
    if (g_buffer != g_initial_buffer)
        growth_watermark += BOOT_ALLOC_WORST_CASE_GROWTH_PER_ALLOCATION;

    if ((g_capacity - g_entry_count) >= growth_watermark)
        return true;

    new_capacity = PAGE_ROUND_UP(g_capacity * 2 * sizeof(struct memory_range));

    addr = boot_alloc_nogrow(new_capacity >> PAGE_SHIFT);
    if (WARN_ON(addr == 0))
        return false;

    new_buffer = phys_to_virt(addr);
    memcpy(new_buffer, g_buffer, g_entry_count * sizeof(*g_buffer));

    if (g_buffer != g_initial_buffer) {
        size_t byte_capacity;

        byte_capacity = PAGE_ROUND_UP(g_capacity * sizeof(struct memory_range));
        boot_free(virt_to_phys(g_buffer), byte_capacity >> PAGE_SHIFT);
    }

    g_buffer = new_buffer;
    g_capacity = new_capacity / sizeof(struct memory_range);
    return true;
}

static error_t range_append(struct memory_range *mr)
{
    if (unlikely(!maybe_grow_buffer()))
        return ENOMEM;

    range_emplace_at(g_entry_count, mr);
    return EOK;
}

phys_addr_or_error_t boot_alloc(size_t num_pages)
{
    if (unlikely(!maybe_grow_buffer()))
        return encode_error_phys_addr(ENOMEM);

    return boot_alloc_nogrow(num_pages);
}

phys_addr_or_error_t boot_alloc_at(phys_addr_t address, size_t num_pages)
{
    if (unlikely(!maybe_grow_buffer()))
        return encode_error_phys_addr(ENOMEM);

    return allocate_within(
        num_pages, address, address + (num_pages << PAGE_SHIFT)
    );
}

void boot_free(phys_addr_t address, size_t num_pages)
{
    ssize_t mr_idx;

    struct memory_range freed_range = {
        .physical_address = address,
        .size_and_type = MR_ENCODE(num_pages * PAGE_SIZE, MEMORY_FREE),
    };

    if (unlikely(!maybe_grow_buffer())) {
        pr_warn("leaking memory at 0x%016llX (%zu pages)\n", address, num_pages);
        return;
    }

    mr_idx = find_range(address, ALLOW_ONE_ABOVE_NO);
    BUG_ON_WITH_MSG(
        mr_idx < 0, "invalid free at 0x%016llX (%zu pages)\n",
        address, num_pages
    );

    allocate_out_of(mr_idx, &freed_range);
}

void boot_alloc_init(void)
{
    struct ultra_memory_map_attribute *mm;
    struct ultra_memory_map_entry *entry;
    struct memory_range range;
    u8 type;
    size_t i;

    mm = g_boot_ctx.memory_map;

    for (i = 0; i < ULTRA_MEMORY_MAP_ENTRY_COUNT(mm->header); i++) {
        entry = &mm->entries[i];

        switch (entry->type) {
        case ULTRA_MEMORY_TYPE_RECLAIMABLE:
        case ULTRA_MEMORY_TYPE_KERNEL_BINARY:
        case ULTRA_MEMORY_TYPE_LOADER_RECLAIMABLE:
            type = MEMORY_ALLOCATED;
            break;
        case ULTRA_MEMORY_TYPE_FREE:
            type = MEMORY_FREE;
            break;

        default:
            continue;
        }

        range.physical_address = entry->physical_address;
        range.size_and_type = MR_ENCODE(entry->size, type);

        if (type == MEMORY_FREE) {
            pr_info(
                "adding memory 0x%016llX -> 0x%016llX\n",
                range.physical_address, range.physical_address + MR_SIZE(&range)
            );
        }

        WARN_ON(range_append(&range));
    }
}
