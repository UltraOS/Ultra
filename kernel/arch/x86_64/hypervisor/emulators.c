#include <arch/private/hypervisor.h>
#include <arch/private/cpu.h>

#include <smbios.h>

static void setup_emulator(struct active_hypervisor *hv)
{
    UNREFERENCED_PARAMETER(hv);

    // Emulators don't have a reliable TSC
    all_cpus_disable(X86_FEATURE_TSC_RELIABLE);
}

HYPERVISOR s_tcg = {
    .name = "QEMU with TCG",
    .type = HYPERVISOR_TYPE_QEMU_TCG,
    .cpuid_signature = "TCGTCGTCGTCG",
    .max_leaf = MAX_HYPERVISOR_LEAF,
    .setup = setup_emulator,
};

static u32 bochs_detect(void)
{
    return bios_version_check("Bochs");
}

HYPERVISOR s_bochs = {
    .name = "Bochs",
    .type = HYPERVISOR_TYPE_BOCHS,
    .detect = bochs_detect,
    .setup = setup_emulator,
};
