#define MSG_FMT(msg) "per-cpu: " msg

#include <boot/alloc.h>

#include <linker.h>
#include <per_cpu.h>
#include <log.h>
#include <bug.h>
#include <free_after_init.h>
#include <config.h>

#include <common/string.h>
#include <common/error.h>
#include <common/align.h>

#include <memory/io.h>

#include <private/per_cpu.h>

ptr_t g_per_cpu_offset[ULTRA_MAX_CPUS];
virt_addr_t g_per_cpu_base;

extern u8 SECTION_MARKER_BEGIN(PER_CPU_SECTION)[];
extern u8 SECTION_MARKER_END(PER_CPU_SECTION)[];

void INIT_CODE per_cpu_setup(void)
{
    size_t static_size, per_cpu_size, alloc_size, i;
    ptr_t this_cpu_offset;
    void *this_cpu_ptr;
    phys_addr_or_error_t addr;

    static_size = SECTION_SIZE(PER_CPU_SECTION);
    per_cpu_size = PAGE_ROUND_UP(static_size);
    alloc_size = per_cpu_size * g_num_present_cpus;

    pr_info(
        "static size: %zu, per-cpu size: %zu, total: %zu\n",
        static_size, per_cpu_size, alloc_size
    );

    addr = boot_alloc(alloc_size);
    if (error_phys_addr(addr)) {
        panic(
            "Unable to allocate the initial per-cpu area: %d",
            decode_error_phys_addr(addr)
        );
    }

    this_cpu_ptr = phys_to_virt(addr);
    g_per_cpu_base = (ptr_t)this_cpu_ptr;

    pr_info("base set at 0x%016zX\n", g_per_cpu_base);

    /*
     * The offset is the delta between the initial static per-cpu section
     * and the new allocated per-cpu section, which is duplicated for each CPU.
     */
    this_cpu_offset = g_per_cpu_base;
    this_cpu_offset -= (ptr_t)SECTION_MARKER_BEGIN(PER_CPU_SECTION);

    for (i = 0; i < g_num_present_cpus; i++) {
        g_per_cpu_offset[i] = this_cpu_offset;

        // Give every CPU its own copy of the static per-cpu section
        memcpy(
            this_cpu_ptr, SECTION_MARKER_BEGIN(PER_CPU_SECTION), static_size
        );

        this_cpu_offset += per_cpu_size;
        this_cpu_ptr += per_cpu_size;
    }
}
