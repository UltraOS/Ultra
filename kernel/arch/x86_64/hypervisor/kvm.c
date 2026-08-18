#define MSG_FMT(msg) "kvm: " msg

#include <arch/private/hypervisor.h>
#include <arch/private/kvm.h>
#include <arch/private/cpu.h>
#include <arch/private/msr.h>
#include <arch/private/tsc.h>

#include <time/pvclock.h>
#include <boot/alloc.h>
#include <memory/io.h>

#include <free_after_init.h>
#include <log.h>
#include <init_level.h>

static struct kvm_ctx {
    u32 features;
    u32 perf_hints;

    bool has_pvclock;
    u32 pvclock_system_time_msr;
    u32 pvclock_wall_clock_msr;

    phys_addr_t time_info_base;
} s_ctx;

static bool kvm_has_feature(u32 mask)
{
    return (s_ctx.features & mask) == mask;
}

static INIT_CODE void kvm_features_detect(struct active_hypervisor *hv)
{
    struct cpuid_res res;

    cpuid(hv->cpuid_base | KVM_CPUID_FEATURES, &res);
    s_ctx.features = res.a;
    s_ctx.perf_hints = res.d;

    if (kvm_has_feature(KVM_FEATURE_MSI_EXT_DEST_ID))
        hv->features |= HYPERVISOR_FEATURE_MSI_EXT_DEST_ID;

    if (kvm_has_feature(KVM_FEATURE_NOP_IO_DELAY))
        hv->features |= HYPERVISOR_FEATURE_SKIP_PORT_IO_DELAY;

    if (kvm_has_feature(KVM_FEATURE_CLOCKSOURCE)) {
        s_ctx.has_pvclock = true;
        s_ctx.pvclock_wall_clock_msr = MSR_KVM_WALL_CLOCK;
        s_ctx.pvclock_system_time_msr = MSR_KVM_SYSTEM_TIME;
    }

    if (kvm_has_feature(KVM_FEATURE_CLOCKSOURCE2)) {
        s_ctx.has_pvclock = true;
        s_ctx.pvclock_wall_clock_msr = MSR_KVM_WALL_CLOCK_NEW;
        s_ctx.pvclock_system_time_msr = MSR_KVM_SYSTEM_TIME_NEW;
    }
}

static void kvm_start_pvclock(void)
{
    struct pvclock_vcpu_time_info *time_info;

    time_info = this_cpu_read(g_this_cpu_time_info);

    wrmsr_or_die(
        s_ctx.pvclock_system_time_msr,
        virt_to_phys(time_info) | KVM_MSR_ENABLED
    );

    pr_info("pvclock started for CPU%u\n", unstable_cpu_id());
}

static INIT_CODE error_t kvm_setup_pvclock(void)
{
    size_t bytes_to_allocate;
    phys_addr_or_error_t time_info_base;
    void *this_info;

    if (!s_ctx.has_pvclock)
        return EOK;

    pr_info(
        "pvclock at 0x%08X/0x%08X\n",
        s_ctx.pvclock_wall_clock_msr, s_ctx.pvclock_system_time_msr
    );

    BUILD_BUG_ON(sizeof(struct pvclock_vcpu_time_info) > CACHE_LINE_SIZE);
    bytes_to_allocate = PAGE_ROUND_UP(CACHE_LINE_SIZE * g_num_present_cpus);

    time_info_base = boot_alloc(bytes_to_allocate >> PAGE_SHIFT);
    if (error_phys_addr(time_info_base))
        return decode_error_phys_addr(time_info_base);

    if (kvm_has_feature(KVM_FEATURE_CLOCKSOURCE_STABLE_BIT))
        pvclock_enable_stable_bit();

    this_info = phys_to_virt(time_info_base);
    this_cpu_write(g_this_cpu_time_info, this_info);
    s_ctx.time_info_base = time_info_base;

    kvm_start_pvclock();
    tsc_set_known_frequency(pvclock_calculate_tsc_hz(this_info), "pvclock");
    return EOK;
}
// The pvclock area is sized by the number of vCPUs that will be onlined
INIT_CALL_POST(X86_PLATFORM_INFO_AVAILABLE, kvm_setup_pvclock);

static INIT_CODE void kvm_setup(struct active_hypervisor *hv)
{
    kvm_features_detect(hv);
}

HYPERVISOR s_kvm = {
    .name = "KVM",
    .type = HYPERVISOR_TYPE_KVM,
    .setup = kvm_setup,
    .cpuid_signature = "KVMKVMKVM",
    .max_leaf = MAX_HYPERVISOR_LEAF,
};
