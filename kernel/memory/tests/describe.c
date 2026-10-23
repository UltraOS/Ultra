#include <ktest/ktest.h>

#include <common/format.h>
#include <common/string.h>

#include <memory/alloc.h>
#include <memory/buddy.h>
#include <memory/describe.h>
#include <memory/page.h>

#include <per_cpu.h>
#include <smp.h>
#include <symbols.h>

#define LARGE_ALLOCATION_SIZE (4 * PAGE_SIZE)
#define LARGE_ALLOCATION_ORDER 2u

struct described {
    u64 first;
    u64 second;
};

static u64 s_described_data[4];
static DEFINE_PER_CPU(struct described, s_described_per_cpu);

static bool starts_with(const char *str, const char *prefix)
{
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

static bool ends_with(const char *str, const char *suffix)
{
    size_t len, suffix_len;

    len = strlen(str);
    suffix_len = strlen(suffix);

    return len >= suffix_len && strcmp(str + len - suffix_len, suffix) == 0;
}

KTEST_CASE(image_addresses_are_named_by_symbol)
{
    char buf[MAX_MEM_DESCRIPTION_LENGTH] = { 0 };
    char expected[MAX_MEM_DESCRIPTION_LENGTH];
    char name[MAX_SYMBOL_LENGTH];
    void *past_end, *object;
    size_t symbol_size;
    error_t ret;
    reg_t pc;

    ret = symbol_lookup_code_by_address(
        (ptr_t)mem_describe, name, nullptr, &symbol_size
    );
    KTEST_ASSERT_EQ(ret, EOK);
    snprintf(
        expected, sizeof(expected), "mem_describe+0x1/0x%zx", symbol_size
    );

    mem_describe((char*)mem_describe + 1, buf);
    KTEST_EXPECT_STREQ(buf, expected);

    pc = (reg_t)mem_describe;
    snprintf(buf, sizeof(buf), "%pSM", &pc);
    KTEST_EXPECT_STREQ(buf, "mem_describe");

    pc = (reg_t)mem_describe + 1;
    snprintf(buf, sizeof(buf), "%pSM", &pc);
    snprintf(expected, sizeof(expected), "mem_describe+1/%zu", symbol_size);
    KTEST_EXPECT_STREQ(buf, expected);

    pc = (reg_t)mem_describe + symbol_size;
    snprintf(buf, sizeof(buf), "%pRSM", &pc);
    snprintf(
        expected, sizeof(expected), "mem_describe+%zu/%zu", symbol_size,
        symbol_size
    );
    KTEST_EXPECT_STREQ(buf, expected);

    mem_describe(&s_described_data[2], buf);
    KTEST_EXPECT(starts_with(buf, "s_described_data"));
    KTEST_EXPECT(ends_with(buf, "+0x10/0x20"));

    past_end = &s_described_data[ARRAY_SIZE(s_described_data)];
    mem_describe(past_end, buf);
    KTEST_EXPECT(!starts_with(buf, "s_described_data"));

    object = alloc(16, ALLOC_GENERIC);
    KTEST_ASSERT(object != nullptr);
    mem_describe(object, buf);
    KTEST_EXPECT(starts_with(buf, "kheap "));
    free(object);
}

KTEST_CASE(per_cpu_addresses_are_rebased_onto_the_variable)
{
    char buf[MAX_MEM_DESCRIPTION_LENGTH];
    char expected[MAX_MEM_DESCRIPTION_LENGTH];
    struct described *described;
    cpu_t cpu;

    for_each_present_cpu(cpu) {
        described = per_cpu_ptr(&s_described_per_cpu, cpu);
        mem_describe(&described->second, buf);

        snprintf(
            expected, sizeof(expected),
            "s_described_per_cpu+0x%zx/0x%zx on CPU %u",
            offsetof(struct described, second), sizeof(struct described), cpu
        );
        KTEST_EXPECT_STREQ(buf, expected);
    }

    mem_describe(&s_described_per_cpu, buf);
    snprintf(
        expected, sizeof(expected), "s_described_per_cpu+0x0/0x%zx",
        sizeof(struct described)
    );
    KTEST_EXPECT_STREQ(buf, expected);
}

KTEST_CASE(heap_addresses_name_the_object)
{
    char buf[MAX_MEM_DESCRIPTION_LENGTH];
    char expected[MAX_MEM_DESCRIPTION_LENGTH];
    struct page_block *block;
    char *small, *large;

    small = alloc(100, ALLOC_GENERIC);
    KTEST_ASSERT(small != nullptr);

    mem_describe(small + 8, buf);
    snprintf(
        expected, sizeof(expected), "kheap builtin_128 object %p +0x8", small
    );
    KTEST_EXPECT_STREQ(buf, expected);
    free(small);

    large = alloc(LARGE_ALLOCATION_SIZE, ALLOC_GENERIC);
    KTEST_ASSERT(large != nullptr);

    mem_describe(large + 0x10, buf);
    snprintf(
        expected, sizeof(expected), "kheap large allocation %p order %u +0x10",
        large, LARGE_ALLOCATION_ORDER
    );
    KTEST_EXPECT_STREQ(buf, expected);
    free(large);

    block = alloc_block(0, ALLOC_GENERIC);
    KTEST_ASSERT(block != nullptr);
    mem_describe(block_to_virt(block), buf);
    KTEST_EXPECT(starts_with(buf, "generic block "));
    block_unref(block);
}

KTEST_CASE(direct_map_addresses_name_the_block)
{
    char buf[MAX_MEM_DESCRIPTION_LENGTH];
    char expected[MAX_MEM_DESCRIPTION_LENGTH];
    struct page_block *block;
    char *inside, *tail_page;
    pfn_t pfn;

    block = alloc_block(1, ALLOC_GENERIC);
    KTEST_ASSERT(block != nullptr);
    pfn = page_to_pfn(block_to_page(block));

    inside = (char*)block_to_virt(block) + PAGE_SIZE + 0x20;
    mem_describe(inside, buf);
    snprintf(
        expected, sizeof(expected), "generic block pfn 0x%zx order 1 +0x%zx",
        pfn, (size_t)PAGE_SIZE + 0x20
    );
    KTEST_EXPECT_STREQ(buf, expected);

    tail_page = (char*)&block_to_page(block)[1] + 8;
    mem_describe(tail_page, buf);
    snprintf(
        expected, sizeof(expected),
        "struct page of pfn 0x%zx +0x8, generic block pfn 0x%zx order 1",
        pfn + 1, pfn
    );
    KTEST_EXPECT_STREQ(buf, expected);
    block_unref(block);

    mem_describe((void*)0x1234, buf);
    KTEST_EXPECT_STREQ(buf, "unknown memory");
}

KTEST_CASE(mem_describe_backs_the_format)
{
    char buf[MAX_MEM_DESCRIPTION_LENGTH];
    char expected[MAX_MEM_DESCRIPTION_LENGTH];
    char *object;

    object = alloc(100, ALLOC_GENERIC);
    KTEST_ASSERT(object != nullptr);

    mem_describe(object + 8, expected);
    snprintf(buf, sizeof(buf), "%pMEM", object + 8);
    KTEST_EXPECT_STREQ(buf, expected);
    free(object);

    snprintf(buf, sizeof(buf), "%pMEM", (void*)0x1234);
    KTEST_EXPECT_STREQ(buf, "unknown memory");

    mem_describe(nullptr, buf);
    KTEST_EXPECT_STREQ(buf, "unknown memory");
}
