#include <arch/private/hypervisor.h>
#include <arch/private/cpu.h>

#include <log.h>

#define BHYVE_FEATURES_LEAF (BASE_HYPERVISOR_LEAF | 1)

#define BHYVE_FEATURE_MSI_EXT_DEST_ID BIT_U32(0)

static void bhyve_setup(struct active_hypervisor *hv)
{
    struct cpuid_res res;

    if (unlikely(hv->max_leaf < BHYVE_FEATURES_LEAF)) {
        pr_warn("bhyve: no feature leaf\n");
        return;
    }

    cpuid(hv->cpuid_base | BHYVE_FEATURES_LEAF, &res);

    if (res.a & BHYVE_FEATURE_MSI_EXT_DEST_ID)
        hv->features |= HYPERVISOR_FEATURE_MSI_EXT_DEST_ID;
}

HYPERVISOR s_bhyve = {
    .name = "Bhyve",
    .type = HYPERVISOR_TYPE_BHYVE,
    .cpuid_signature = "bhyve bhyve ",
    .max_leaf = MAX_HYPERVISOR_LEAF,
    .setup = bhyve_setup,
};
