#include <memory/page_table.h>

bool g_la57 = false;
u64 g_pt5_shift = PT4_SHIFT;
u64 g_pt4_num_entries = 1;

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
