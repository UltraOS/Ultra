#include <cstdlib>
#include <cstring>
#include <set>
#include <filesystem>

#include "test_harness.h"
#include "test_helpers.h"

class phys_range {
public:
    phys_range(uint64_t start, uint64_t size)
        : start(start), size(size), virt(std::malloc(size))
    {
    }

    ~phys_range()
    {
        std::free(virt);
    }

    bool contains_virt(void* tgt) const
    {
        uintptr_t this_virt = (uintptr_t)virt;
        uintptr_t target_virt = (uintptr_t)tgt;

        if (target_virt < this_virt)
            return false;

        if ((this_virt + size) <= target_virt)
            return false;

        return true;
    }

    bool contains_phys(uint64_t tgt) const
    {
        if (tgt < start)
            return false;

        if ((start + size) <= tgt)
            return false;

        return true;
    }

    uint64_t phys_from_virt(void* tgt) const
    {
        return start + ((uintptr_t)tgt - (uintptr_t)virt);
    }

    void* virt_from_phys(uint64_t tgt) const
    {
        return (void*)(((uintptr_t)virt) + (tgt - start));
    }

public:
    uint64_t start, size;
    void* virt;
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

void malloc_phys_range(uint64_t start, uint64_t size)
{
    g_phys_ranges.emplace(start, size);
}

void reset_phys_ranges()
{
    g_phys_ranges.clear();
}

uint64_t translate_virt_to_phys(void* virt)
{
    for (const auto& range : g_phys_ranges) {
        if (!range.contains_virt(virt))
            continue;

        return range.phys_from_virt(virt);
    }

    throw std::runtime_error(
        "Unable to lookup virt=" + std::to_string((uintptr_t)virt)
    );
}

void* translate_phys_to_virt(uint64_t phys)
{
    auto it = g_phys_ranges.upper_bound(phys);

    if (it == g_phys_ranges.begin() || !(--it)->contains_phys(phys))
        throw std::runtime_error(
            "Unable to lookup phys=" + std::to_string(phys)
        );

    return it->virt_from_phys(phys);
}

std::unordered_map<std::string, std::vector<test_case*>> g_test_groups;

void add_test_case(struct test_case *test, const char *file)
{
    // Transform the absolute path like /foo/bar/test_baz.c into test_baz
    auto filename =
        std::filesystem::path(file).filename()
                                   .replace_extension()
                                   .string();

    // 2. Transform test_baz into baz
    if (filename.starts_with("test_"))
        filename.erase(0, std::strlen("test_"));

    g_test_groups[filename].push_back(test);
}
