#include <cstdlib>
#include <cstring>
#include <set>
#include <filesystem>

#ifdef __APPLE__
#include <mach/mach_time.h>
#else
#include <ctime>
#endif

/*
 * Included after any host headers so that the synthetic <arch/constants.h>
 * (pulled in here) overrides a possibly leaked host PAGE_SIZE.
 */
#include <common/align.h>

static_assert(PAGE_SIZE == 4096, "host PAGE_SIZE leaked into the harness");

/*
 * struct page pulls in <common/bit.h>'s meta bit-field macros, which are not
 * valid C++, so the memory map is owned by memory_map.c and driven here through
 * this plain-integer API. See that file for the rationale.
 */
extern "C" {
    void memory_map_reserve(uint64_t phys_end);
    void memory_map_reset(void);
}

#include "test_harness.h"
#include "test_helpers.h"

class phys_range {
public:
    phys_range(uint64_t start, uint64_t size)
        : start(start), size(size)
    {
    }

    bool contains_phys(uint64_t tgt) const
    {
        if (tgt < start)
            return false;

        if ((start + size) <= tgt)
            return false;

        return true;
    }

public:
    uint64_t start, size;
};

struct phys_range_cmp {
    using is_transparent = int;

    bool operator()(const phys_range& lhs, const phys_range& rhs) const
    {
        return lhs.start < rhs.start;
    }

    bool operator()(const phys_range& lhs, uint64_t tgt) const
    {
        return lhs.start < tgt;
    }

    bool operator()(uint64_t tgt, const phys_range& rhs) const
    {
        return tgt < rhs.start;
    }
};

static std::set<phys_range, phys_range_cmp> g_phys_ranges;

/*
 * All registered physical ranges share a single contiguous backing buffer that
 * is indexed by absolute physical address (i.e. phys P lives at backing[P]).
 *
 * This is load-bearing for the buddy allocator: it coalesces and splits blocks
 * by computing buddy PFNs and assumes that physically adjacent pages are also
 * adjacent in the direct map. Multi-page slabs likewise hand out objects that
 * straddle page boundaries. A per-range malloc() would place adjacent pages at
 * unrelated virtual addresses and corrupt both.
 *
 * The buffer is page-aligned so that the virtual address of any page-aligned
 * physical address is itself page-aligned, matching the direct map on real
 * hardware. The slab allocator relies on this to keep objects naturally
 * aligned to their (power-of-two) size within a page.
 */
static uint8_t *g_phys_backing;
static uint64_t g_phys_backing_size;

static void backing_reserve(uint64_t needed)
{
    if (g_phys_backing_size >= needed)
        return;

    uint64_t new_size = ALIGN_UP(needed, PAGE_SIZE);
    auto *new_buffer = (uint8_t *)std::aligned_alloc(PAGE_SIZE, new_size);
    if (!new_buffer)
        throw std::runtime_error("failed to allocate physical backing store");

    std::memset(new_buffer, 0, new_size);

    if (g_phys_backing) {
        std::memcpy(new_buffer, g_phys_backing, g_phys_backing_size);
        std::free(g_phys_backing);
    }

    g_phys_backing = new_buffer;
    g_phys_backing_size = new_size;
}

static bool phys_is_mapped(uint64_t phys)
{
    auto it = g_phys_ranges.upper_bound(phys);

    if (it == g_phys_ranges.begin())
        return false;

    return (--it)->contains_phys(phys);
}

void malloc_phys_range(uint64_t start, uint64_t size)
{
    start = ALIGN_DOWN(start, PAGE_SIZE);
    size = ALIGN_UP(size, PAGE_SIZE);

    auto [it, inserted] = g_phys_ranges.emplace(start, size);
    bool valid = inserted;

    if (valid && it != g_phys_ranges.begin()) {
        auto prev = std::prev(it);
        valid = (prev->start + prev->size) <= start;
    }
    if (valid) {
        auto next = std::next(it);
        valid = next == g_phys_ranges.end() || (start + size) <= next->start;
    }

    if (!valid)
        throw std::runtime_error(
            "phys range at " + std::to_string(start) +
            " overlaps an already registered one"
        );

    uint64_t end = start + size;
    backing_reserve(end);
    memory_map_reserve(end);
}

void reset_phys_ranges()
{
    g_phys_ranges.clear();

    std::free(g_phys_backing);
    g_phys_backing = nullptr;
    g_phys_backing_size = 0;

    memory_map_reset();
}

uint64_t translate_virt_to_phys(void* virt)
{
    uintptr_t base = (uintptr_t)g_phys_backing;
    uintptr_t target = (uintptr_t)virt;

    if (target < base || target >= base + g_phys_backing_size)
        throw std::runtime_error(
            "Unable to lookup virt=" + std::to_string(target)
        );

    uint64_t phys = target - base;
    if (!phys_is_mapped(phys))
        throw std::runtime_error(
            "virt=" + std::to_string(target) + " maps to unbacked phys=" +
            std::to_string(phys)
        );

    return phys;
}

void* translate_phys_to_virt(uint64_t phys)
{
    if (!phys_is_mapped(phys))
        throw std::runtime_error(
            "Unable to lookup phys=" + std::to_string(phys)
        );

    return g_phys_backing + phys;
}

std::unordered_map<std::string, test_group>& test_groups()
{
    static std::unordered_map<std::string, test_group> groups;
    return groups;
}

static auto file_to_test_group_name(const char *file)
{
    // 1. Transform the absolute path like /foo/bar/test_baz.c into test_baz
    auto filename = std::filesystem::path(file).filename()
                                               .replace_extension()
                                               .string();
    // 2. Transform test_baz into baz
    if (filename.starts_with("test_"))
        filename.erase(0, std::strlen("test_"));

    return filename;
}

void add_test_case(struct test_case *test, const char *file)
{
    auto group = file_to_test_group_name(file);
    test_groups()[group].test_cases.push_back(test);
}

void teardown_callback_register(void (*callback)(void), const char *file)
{
    auto group = file_to_test_group_name(file);
    test_groups()[group].teardown_callback = callback;
}

#define NANOSECONDS_PER_SECOND 1000000000ull

uint64_t ns_timer()
{
#if defined(__APPLE__)
    static struct mach_timebase_info tb;
    static bool initialized;

    if (!initialized) {
        ASSERT_EQ(mach_timebase_info(&tb), KERN_SUCCESS);
        initialized = true;
    }

    return (mach_absolute_time() * tb.numer) / tb.denom;
#else
    struct timespec ts;

    ASSERT_EQ(clock_gettime(CLOCK_MONOTONIC, &ts), 0);
    return ts.tv_sec * NANOSECONDS_PER_SECOND + ts.tv_nsec;
#endif
}
