#include <arch/private/cpu.h>
#include <arch/private/cr.h>
#include <arch/private/msr.h>

#include <common/bit.h>
#include <memory/page_table.h>
#include <boot/boot.h>

#include <free_after_init.h>
#include <init_level.h>

bool g_la57 = false;
u64 g_pt5_shift = PT4_SHIFT;
u64 g_pt4_num_entries = 1;

ptr_t g_supported_pt_bits = UNSIGNED_MAX(ptr_t);

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
        g_supported_pt_bits &= ~X86_PT_GLOBAL;
    }
    cr4_feature_enable(cr4_features);

    g_have_gb_pages = all_cpus_have(X86_FEATURE_PDPE1GB);

    if (all_cpus_have(X86_FEATURE_NX))
        efer_feature_enable(IA32_EFER_NX);
    else
        g_supported_pt_bits &= ~X86_PT_NX;

    return EOK;
}
INIT_CALL_POST(BOOT_INFO_AVAILABLE, x86_early_paging_init);

struct pt_prot pt_prot_from_vm_prot(enum vm_prot vm_prot)
{
    struct pt_prot pt_prot = { 0 };

    if (vm_prot == VM_PROT_NONE)
        return pt_prot;

    if (vm_prot & VM_PROT_READ)
        pt_prot.value |= X86_PT_PRESENT;

    if (vm_prot & VM_PROT_WRITE)
        pt_prot.value |= X86_PT_WRITE;

    if (!(vm_prot & VM_PROT_EXEC))
        pt_prot.value |= X86_PT_NX;

    if (!(vm_prot & VM_PROT_KERNEL))
        pt_prot.value |= X86_PT_USER;
    else
        pt_prot.value |= X86_PT_GLOBAL;

    pt_prot.value &= g_supported_pt_bits;
    return pt_prot;
}

struct pt4 *pt4_from_pt5(struct pt5 *pt5, virt_addr_t addr)
{
    struct pt4 *pt4;

    if (!g_la57)
        return (struct pt4*)pt5;

    pt4 = pt5_to_virt(pt5);
    return &pt4[pt4_index(addr)];
}
