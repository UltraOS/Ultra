#include <memory/page.h>
#include <memory/buddy.h>
#include <private/buddy.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * The test memory map lives in this C translation unit on purpose: struct
 * page pulls in the meta bit-field helpers from <common/bit.h>, which are not
 * valid C++. The C++ harness (test_harness.cpp) owns only the raw physical
 * backing buffer and drives the memory map through the plain-integer API
 * declared here.
 *
 * The padding satisfies the population contract described next to
 * BUDDY_MAX_SIZE: merge probing may look up to one window past the top
 * of the registered memory, and the entries it reads must exist and
 * stay zeroed, i.e. PAGE_TYPE_RESERVED, so they are never mistaken for
 * free buddies.
 */
#define MEMORY_MAP_PADDING_PAGES PHYS_ADDR_TO_PFN(BUDDY_MAX_SIZE)

struct page *g_memory_map;
static uint64_t s_num_pages;

/*
 * Ensure the memory map covers every page up to 'phys_end' plus the buddy
 * padding, growing (never shrinking) and zeroing freshly added entries. The
 * backing storage may move, g_memory_map is republished on every growth.
 */
void memory_map_reserve(uint64_t phys_end)
{
    uint64_t num_pages = (phys_end / PAGE_SIZE) + MEMORY_MAP_PADDING_PAGES;
    struct page *grown;

    if (num_pages <= s_num_pages)
        return;

    grown = realloc(g_memory_map, num_pages * sizeof(struct page));
    if (grown == nullptr)
        abort();

    memset(
        &grown[s_num_pages], 0,
        (num_pages - s_num_pages) * sizeof(struct page)
    );

    g_memory_map = grown;
    s_num_pages = num_pages;
}

void memory_map_reset(void)
{
    free(g_memory_map);
    g_memory_map = nullptr;
    s_num_pages = 0;
}
