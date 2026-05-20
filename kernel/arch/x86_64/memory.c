#include <arch/private/cpu.h>
#include <arch/private/cr.h>
#include <arch/private/msr.h>

#include <common/bit.h>
#include <boot/boot.h>

#include <memory/page_table.h>
#include <memory/address_space.h>
#include <memory/tlb.h>

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

static error_t INIT_CODE x86_early_paging_init(void)
{
    reg_t cr4_features = 0;

    // Just stick with the page table depth our bootloader has picked
    if (g_boot_ctx.platform_info->page_table_depth == 5) {
        BUG_ON(!all_cpus_have(X86_FEATURE_LA57));
        g_la57 = true;
        g_pt4_num_entries = 512;
        g_pt5_shift += X86_PT_LVL_SHIFT;
    } else {
        BUG_ON(g_boot_ctx.platform_info->page_table_depth != 4);
    }

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

struct pt4 *pt4_from_pt5(struct pt5 *pt5, virt_addr_t addr)
{
    struct pt4 *pt4;

    if (!g_la57)
        return (struct pt4*)pt5;

    pt4 = pt5_to_virt(pt5);
    return &pt4[pt4_index(addr)];
}

void tlb_invalidate_kernel_range(virt_addr_t va_start, virt_addr_t va_end)
{
    while (va_start < va_end) {
        asm volatile("invlpg (%0)" :: "r" (va_start) : "memory");
        va_start += PT1_SIZE;
    }
}
