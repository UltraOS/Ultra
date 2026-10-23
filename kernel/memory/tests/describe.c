#include <ktest/ktest.h>

#include <common/format.h>
#include <common/string_container.h>

#include <memory/alloc.h>
#include <memory/buddy.h>
#include <memory/describe.h>
#include <memory/page.h>

#include <per_cpu.h>
#include <smp.h>
#include <symbols.h>

#define LARGE_ALLOCATION_SIZE (4 * PAGE_SIZE)
#define LARGE_ALLOCATION_ORDER 2u

static char s_got[MAX_MEM_DESCRIPTION_LENGTH];
static char s_expected[MAX_MEM_DESCRIPTION_LENGTH];

/*
 * Expect 'got' to equal the string that str_printf() makes from 'fmt' and the
 * arguments after it.
 */
#define EXPECT_FORMATTED(got, fmt, ...) do {               \
    struct string expect_formatted_str;                    \
                                                           \
    expect_formatted_str = str_printf(                     \
        s_expected, sizeof(s_expected), fmt, ##__VA_ARGS__ \
    );                                                     \
    KTEST_EXPECT_EQ(got, expect_formatted_str);            \
} while (0)

// EXPECT_FORMATTED() for the description of 'addr', a pointer or a ptr_t
#define EXPECT_DESCRIBED(addr, fmt, ...)                             \
    EXPECT_FORMATTED(                                                \
        mem_describe((const void*)(addr), s_got), fmt, ##__VA_ARGS__ \
    )

// EXPECT_FORMATTED() for 'arg' printed with the format 'spec'
#define EXPECT_PRINTED(spec, arg, fmt, ...)                             \
    EXPECT_FORMATTED(                                                   \
        str_printf(s_got, sizeof(s_got), spec, arg), fmt, ##__VA_ARGS__ \
    )

struct described {
    u64 first;
    u64 second;
};

/*
 * LTO renames static variables that exist in other translation units, e.g. x
 * becomes x.lto_priv.0 with GCC or x.1 with clang. The cases below expect
 * the exact names of these two, so both are named after this test to hopefully
 * be unique.
 */
static u64 s_describe_ktest_data[4];
static DEFINE_PER_CPU(struct described, s_describe_ktest_per_cpu);

KTEST_CASE(image_addresses_are_named_by_symbol)
{
    char name_buf[MAX_SYMBOL_LENGTH];
    struct string got, name;
    void *past_end, *object;
    size_t symbol_size;
    ptr_t pc, mem_describe_addr = (ptr_t)mem_describe;

    name = symbol_lookup_code_by_address(
        mem_describe_addr, name_buf, nullptr, &symbol_size
    );
    KTEST_ASSERT_EQ(name, STR("mem_describe"));

    EXPECT_DESCRIBED(
        mem_describe_addr + 1, "mem_describe+0x1/0x%zx", symbol_size
    );
    EXPECT_PRINTED("%pSM", &mem_describe_addr, "mem_describe");

    pc = mem_describe_addr + 1;
    EXPECT_PRINTED("%pSM", &pc, "mem_describe+1/%zu", symbol_size);

    pc = mem_describe_addr + symbol_size;
    EXPECT_PRINTED(
        "%pRSM", &pc, "mem_describe+%zu/%zu", symbol_size, symbol_size
    );

    EXPECT_DESCRIBED(
        &s_describe_ktest_data[2], "s_describe_ktest_data+0x10/0x20"
    );

    // The address right after the end of the array isn't part of it
    past_end = &s_describe_ktest_data[ARRAY_SIZE(s_describe_ktest_data)];
    got = mem_describe(past_end, s_got);
    KTEST_EXPECT(!str_starts_with(got, STR("s_describe_ktest_data")));

    object = alloc(16, ALLOC_GENERIC);
    KTEST_ASSERT(object != nullptr);
    EXPECT_DESCRIBED(object, "kheap builtin_16 object %p +0x0", object);
    free(object);
}

KTEST_CASE(per_cpu_addresses_are_rebased_onto_the_variable)
{
    struct described *described;
    cpu_t cpu;

    for_each_present_cpu(cpu) {
        described = per_cpu_ptr(&s_describe_ktest_per_cpu, cpu);
        EXPECT_DESCRIBED(
            &described->second,
            "s_describe_ktest_per_cpu+0x%zx/0x%zx on CPU %u",
            offsetof(struct described, second), sizeof(struct described), cpu
        );
    }

    EXPECT_DESCRIBED(
        &s_describe_ktest_per_cpu, "s_describe_ktest_per_cpu+0x0/0x%zx",
        sizeof(struct described)
    );
}

KTEST_CASE(heap_addresses_name_the_object)
{
    struct page_block *block;
    char *small, *large;

    small = alloc(100, ALLOC_GENERIC);
    KTEST_ASSERT(small != nullptr);

    EXPECT_DESCRIBED(small + 8, "kheap builtin_128 object %p +0x8", small);
    free(small);

    large = alloc(LARGE_ALLOCATION_SIZE, ALLOC_GENERIC);
    KTEST_ASSERT(large != nullptr);

    EXPECT_DESCRIBED(
        large + 0x10, "kheap large allocation %p order %u +0x10", large,
        LARGE_ALLOCATION_ORDER
    );
    free(large);

    block = alloc_page(ALLOC_GENERIC);
    KTEST_ASSERT(block != nullptr);
    EXPECT_DESCRIBED(
        block_to_virt(block), "generic block pfn 0x%zx order 0 +0x0",
        page_to_pfn(block_to_page(block))
    );
    block_unref(block);
}

KTEST_CASE(direct_map_addresses_name_the_block)
{
    struct page_block *block;
    char *inside, *tail_page;
    pfn_t pfn;

    block = alloc_block(1, ALLOC_GENERIC);
    KTEST_ASSERT(block != nullptr);
    pfn = page_to_pfn(block_to_page(block));

    inside = (char*)block_to_virt(block) + PAGE_SIZE + 0x20;
    EXPECT_DESCRIBED(
        inside, "generic block pfn 0x%zx order 1 +0x%zx", pfn, PAGE_SIZE + 0x20
    );

    tail_page = (char*)&block_to_page(block)[1] + 8;
    EXPECT_DESCRIBED(
        tail_page,
        "struct page of pfn 0x%zx +0x8, generic block pfn 0x%zx order 1",
        pfn + 1, pfn
    );
    block_unref(block);

    EXPECT_DESCRIBED(0x1234, "unknown memory");
}

KTEST_CASE(mem_describe_backs_the_format)
{
    char *object;

    object = alloc(100, ALLOC_GENERIC);
    KTEST_ASSERT(object != nullptr);

    EXPECT_DESCRIBED(object + 8, "%pMEM", object + 8);
    free(object);

    EXPECT_PRINTED("%pMEM", (void*)0x1234, "unknown memory");
    EXPECT_DESCRIBED(nullptr, "unknown memory");
}
