#include <arch/private/cpu.h>
#include <arch/private/cr.h>
#include <arch/private/msr.h>
#include <arch/memory.h>

#include <common/bit.h>
#include <boot/boot.h>

#include <memory/page_table.h>
#include <memory/address_space.h>
#include <memory/tlb.h>
#include <memory/io.h>
#include <memory/units.h>
#include <memory/page.h>

#include <free_after_init.h>
#include <init_level.h>

bool g_have_gb_pages = false;

bool g_la57 = false;
u64 g_pt5_shift = PT4_SHIFT;
u64 g_pt4_num_entries = 1;

static ptr_t s_supported_pt_bits = UNSIGNED_MAX(ptr_t);

// The default reset value specified in the manuals
static u64 s_pat = IA32_PAT_MAKE(PAT_WB, PAT_WT, PAT_UC_MINUS, PAT_UC,
                                 PAT_WB, PAT_WT, PAT_UC_MINUS, PAT_UC);

// UC- by default
u64 g_wc_pt_prot = X86_PT_UNCACHED;

static void INIT_CODE cache_init(void)
{
    error_t ret;

    if (!all_cpus_have(X86_FEATURE_PAT))
        return;

    ret = rdmsr(MSR_IA32_PAT, &s_pat);
    if (is_error(ret)) {
        // Assume the PAT is using the defaut reset configuration then
        pr_warn("unable to read PAT configuration!\n");
        return;
    }

    /*
     * Set the desired configuration. Why we want this specific config:
     * 1. The bottom 4 slots are kept as is, this means in case PAT is
     *    supported, default PTE flags for WB, WT, UC- and UC remain
     *    completely untouched.
     * 2. Slots 5, 6 are identity mapped to their lower counterparts,
     *    this makes potential errata on very old CPUs that ignore the
     *    PAT bit completely safe. Essentially we never use them so we're
     *    not affected.
     * 3. Slots 7, 8 are changed to WP and WC, since we have to put them
     *    somewhere. Again, in case of the errata this means WP is downgraded
     *    to UC- and WC is downgraded to UC. Technically it's not ideal because
     *    we would want WC downgraded to UC- (so that WC can be enabled via an
     *    MTRR range), but this would require reordering the default flags for
     *    lower "compatibility" slots, so let's roll with this setup.
     */
    s_pat = IA32_PAT_MAKE(PAT_WB, PAT_WT, PAT_UC_MINUS, PAT_UC,
                          PAT_WB, PAT_WT, PAT_WP, PAT_WC);
    wrmsr_or_die(MSR_IA32_PAT, s_pat);

    // WC is entry 7, so 0b111, set all 3 bits
    g_wc_pt_prot = X86_PT_SMALL_PAT | X86_PT_UNCACHED | X86_PT_WRITETHROUGH;
}

/*
 * Cap to 64TiB of RAM maximum by default, since that's all we map
 * in the direct map window. LA57 bumps this up to 4 petabytes below
 * (the theoretical maximum mappable physical address in x86-64 PTEs) since
 * we can afford to map all of it with 57-bits of virtual address space
 * to spare.
 */
#define MAX_PHYS_BITS_NO_LA57 46
#define MAX_PHYS_BITS_LA57 X86_MAX_PHYS_BITS

#define MAX_PHYS_ADDR_NO_LA57 BIT_OF_TYPE(phys_addr_t, MAX_PHYS_BITS_NO_LA57)
#define MAX_PHYS_ADDR_LA57 BIT_OF_TYPE(phys_addr_t, MAX_PHYS_BITS_LA57)

u8 g_max_phys_bits = MAX_PHYS_BITS_NO_LA57;

virt_addr_t g_memory_map_base, g_memory_map_end;
virt_addr_t g_valloc_base, g_valloc_end;

/*
 * 1/64 TiB perfectly cover all 46/52 bits of physical address space we cap
 * !LA57/LA57 CPUs at, (given a struct page is 64 bytes at max). Asserts
 * below ensure the hardcoded size here is enough to fit all possible struct
 * pages.
 */
#define MEMORY_MAP_SIZE_NO_LA57 (1 * TiB)
#define MEMORY_MAP_SIZE_LA57 (64 * TiB)

#define MEMORY_MAP_MAX_NR_PAGES_NO_LA57 (MAX_PHYS_ADDR_NO_LA57 >> PAGE_SHIFT)
#define MEMORY_MAP_MAX_NR_PAGES_LA57 (MAX_PHYS_ADDR_LA57 >> PAGE_SHIFT)

#define MEMORY_MAP_MAX_SIZE_NO_LA57 \
    (MEMORY_MAP_MAX_NR_PAGES_NO_LA57 * sizeof(struct page))
#define MEMORY_MAP_MAX_SIZE_LA57 \
    (MEMORY_MAP_MAX_NR_PAGES_LA57 * sizeof(struct page))

/*
 * Ensure that the memory map size covers worst case
 * (RAM at the very top of the physical address space)
 */
BUILD_BUG_ON_WITH_MSG(
    MEMORY_MAP_SIZE_NO_LA57 < MEMORY_MAP_MAX_SIZE_NO_LA57 ||
    MEMORY_MAP_SIZE_LA57 < MEMORY_MAP_MAX_SIZE_LA57,
    "Memory map is too small to accomodate all possible struct pages"
);

// Random sane pick for !LA57, simple 9 bit scaling for LA57
#define VALLOC_AREA_SIZE_NO_LA57 (32 * TiB)
#define VALLOC_AREA_SIZE_LA57 (16 * PiB)

static error_t INIT_CODE x86_early_paging_init(void)
{
    reg_t cr4_features = 0;
    size_t memmap_size, valloc_area_size;

    /*
     * Just stick with the page table depth our bootloader has picked,
     * but sanity check the protocol-enforced direct map base.
     */
    if (g_boot_ctx.platform_info->page_table_depth == 5) {
        BUG_ON(g_direct_map_base != 0xFF00000000000000);
        BUG_ON(!all_cpus_have(X86_FEATURE_LA57));
        g_la57 = true;
        g_max_phys_bits = MAX_PHYS_BITS_LA57;
        g_pt4_num_entries = 512;
        g_pt5_shift += X86_PT_LVL_SHIFT;

        memmap_size = MEMORY_MAP_SIZE_LA57;
        valloc_area_size = VALLOC_AREA_SIZE_LA57;
    } else {
        BUG_ON(g_direct_map_base != 0xFFFF800000000000);
        BUG_ON(g_boot_ctx.platform_info->page_table_depth != 4);

        memmap_size = MEMORY_MAP_SIZE_NO_LA57;
        valloc_area_size = VALLOC_AREA_SIZE_NO_LA57;
    }

    g_memory_map_base = g_direct_map_base + MAX_PHYS_ADDR;
    g_memory_map_end = g_memory_map_base + memmap_size;

    /*
     * NOTE:
     * 1) +1 here is important as g_memory_map_end is already aligned to the
     *    top level in some configurations
     * 2) PT5_SIZE is correct only after the above if/else branch finishes
     *    setting g_pt5_shift, so don't move it around
     */
    g_valloc_base = ALIGN_UP(g_memory_map_end + 1, PT5_SIZE);
    g_valloc_end = g_valloc_base + valloc_area_size;
    BUG_ON(g_valloc_end > g_boot_ctx.kernel_info->virtual_base);

    /*
     * Enable large & global pages if they're supported
     * PSE is technically ignored in long mode (and assumed to be on), but
     * set it anyway for good measure
     */
    if (all_cpus_have(X86_FEATURE_PSE))
        cr4_features |= X86_CR4_PSE;
    if (all_cpus_have(X86_FEATURE_PGE)) {
        cr4_features |= X86_CR4_PGE;
    } else {
        s_supported_pt_bits &= ~X86_PT_GLOBAL;
    }
    cr4_feature_enable(cr4_features);

    g_have_gb_pages = all_cpus_have(X86_FEATURE_PDPE1GB);

    if (all_cpus_have(X86_FEATURE_NX))
        efer_feature_enable(IA32_EFER_NX);
    else
        s_supported_pt_bits &= ~X86_PT_NX;

    cache_init();

    return EOK;
}
INIT_CALL_POST(BOOT_INFO_AVAILABLE, x86_early_paging_init);

static error_t INIT_CODE x86_load_kernel_pt(void)
{
    cr3_write(virt_to_phys(g_kernel_address_space.pt));
    return EOK;
}
INIT_CALL_POST(KERNEL_ADDRESS_SPACE_AVAILABLE, x86_load_kernel_pt);

pt_prot pt_prot_from_vm_prot(enum vm_prot vm_prot)
{
    pt_prot prot = { 0 };

    if (vm_prot == VM_PROT_NONE)
        return prot;

    if (vm_prot & (VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXEC))
        prot.value |= X86_PT_PRESENT;

    if (vm_prot & VM_PROT_WRITE)
        prot.value |= X86_PT_WRITE;

    if (!(vm_prot & VM_PROT_EXEC))
        prot.value |= X86_PT_NX;

    if (vm_prot & VM_PROT_KERNEL) {
        prot.value |= X86_PT_GLOBAL | X86_PT_ACCESSED;

        if (vm_prot & VM_PROT_WRITE)
            prot.value |= X86_PT_DIRTY;
    } else {
        prot.value |= X86_PT_USER;
    }

    prot.value &= s_supported_pt_bits;
    return prot;
}

void tlb_invalidate_kernel_range(virt_addr_t va_start, virt_addr_t va_end)
{
    while (va_start < va_end) {
        asm volatile("invlpg (%0)" :: "r" (va_start) : "memory");
        va_start += PT1_SIZE;
    }
}
