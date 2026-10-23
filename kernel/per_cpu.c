#define MSG_FMT(msg) "per-cpu: " msg

#include <boot/alloc.h>

#include <linker.h>
#include <per_cpu.h>
#include <smp.h>
#include <log.h>
#include <bug.h>
#include <panic.h>
#include <free_after_init.h>
#include <config.h>
#include <symbols.h>
#include <private/per_cpu.h>

#include <common/string.h>
#include <common/error.h>
#include <common/align.h>
#include <common/format.h>
#include <common/minmax.h>

#include <memory/io.h>

#include <init_level.h>

ptr_t g_per_cpu_offset[ULTRA_MAX_CPUS];
virt_addr_t g_per_cpu_base;

DECLARE_SECTION_MARKERS(PER_CPU_SECTION);

static error_t INIT_CODE per_cpu_setup(void)
{
    size_t static_size, per_cpu_size, alloc_size;
    cpu_t i;
    ptr_t this_cpu_offset;
    void *this_cpu_ptr;
    phys_addr_t addr;
    error_t ret;

    static_size = SECTION_SIZE(PER_CPU_SECTION);
    per_cpu_size = PAGE_ROUND_UP(static_size);
    alloc_size = per_cpu_size * g_num_present_cpus;

    pr_info(
        "static size: %zu, per-cpu size: %zu, total: %zu\n",
        static_size, per_cpu_size, alloc_size
    );

    ret = boot_alloc_bytes(alloc_size, &addr);
    if (is_error(ret))
        panic("Unable to allocate the initial per-cpu area: %pE", &ret);

    this_cpu_ptr = phys_to_virt(addr);
    g_per_cpu_base = (ptr_t)this_cpu_ptr;

    pr_info("base set at 0x%016zX\n", g_per_cpu_base);

    /*
     * The offset is the delta between the initial static per-cpu section
     * and the new allocated per-cpu section, which is duplicated for each CPU.
     */
    this_cpu_offset = g_per_cpu_base;
    this_cpu_offset -= SECTION_MARKER_BEGIN_ADDR(PER_CPU_SECTION);

    for_each_present_cpu(i) {
        g_per_cpu_offset[i] = this_cpu_offset;

        // Give every CPU its own copy of the static per-cpu section
        memcpy(
            this_cpu_ptr, SECTION_MARKER_BEGIN(PER_CPU_SECTION), static_size
        );

        this_cpu_offset += per_cpu_size;
        this_cpu_ptr += per_cpu_size;
    }

    return EOK;
}
INIT_CALL_AT(PER_CPU_AVAILABLE, per_cpu_setup);

int per_cpu_describe(
    const void *addr, char out_desc_buf[MAX_MEM_DESCRIPTION_LENGTH]
)
{
    char name[MAX_SYMBOL_LENGTH];
    size_t offset, symbol_size, per_cpu_size;
    ptr_t begin, template;
    error_t ret;
    cpu_t cpu;

    if (init_level_below(INIT_LEVEL_PER_CPU_AVAILABLE))
        return 0;

    begin = SECTION_MARKER_BEGIN_ADDR(PER_CPU_SECTION);
    per_cpu_size = PAGE_ROUND_UP(SECTION_SIZE(PER_CPU_SECTION));

    /*
     * Every CPU, the boot CPU included, runs on a copy of the section made by
     * per_cpu_setup(). Map the address back onto the section to find the
     * variable.
     */
    for_each_present_cpu(cpu) {
        template = (ptr_t)addr - g_per_cpu_offset[cpu];
        if (!within_bounds(template, begin, begin + per_cpu_size))
            continue;

        ret = symbol_lookup_data_by_address(
            template, name, &offset, &symbol_size
        );
        if (!is_error(ret)) {
            return scnprintf(
                out_desc_buf, MAX_MEM_DESCRIPTION_LENGTH,
                "%s+0x%zx/0x%zx on CPU %u", name, offset, symbol_size, cpu
            );
        }

        return scnprintf(
            out_desc_buf, MAX_MEM_DESCRIPTION_LENGTH,
            "per-CPU area of CPU %u +0x%zx", cpu, template - begin
        );
    }

    return 0;
}
