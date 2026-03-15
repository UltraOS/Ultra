#define MSG_FMT(msg) "x86-smp: " msg

#include <arch/private/smp.h>
#include <arch/private/apic.h>
#include <arch/private/ioapic.h>
#include <arch/private/msr.h>
#include <arch/private/descriptors.h>
#include <arch/private/cpu.h>

#include <private/per_cpu.h>

#include <uacpi/tables.h>
#include <uacpi/acpi.h>

#include <bug.h>
#include <per_cpu.h>
#include <free_after_init.h>

#include <common/error.h>
#include <common/string.h>

#define LAPIC_ID_NONE 0xFF
#define X2APIC_ID_NONE 0xFFFF'FFFF

DEFINE_PER_CPU(u32, g_this_cpu_id);
DEFINE_PER_CPU(u32, g_this_cpu_apic_id);

static INIT_DATA u32 s_early_cpu_to_apic_id[ULTRA_MAX_CPUS] = {
    [0 ... (ULTRA_MAX_CPUS - 1)] = APIC_ID_NONE,
};

static bool s_online_capable_bit_usable;

static INIT_CODE bool cpu_usable(u32 flags)
{
    if (flags & ACPI_PIC_ENABLED)
        return true;

    if (!s_online_capable_bit_usable)
        return false;

    return flags & ACPI_PIC_ONLINE_CAPABLE;
}

struct madt_iter_ctx {
    bool has_valid_lapic_entries;
};

static INIT_CODE uacpi_iteration_decision check_if_madt_has_lapic(
    uacpi_handle user, struct acpi_entry_hdr *hdr
)
{
    struct madt_iter_ctx *ctx = user;

    if (hdr->type == ACPI_MADT_ENTRY_TYPE_LAPIC) {
        struct acpi_madt_lapic *lapic = (struct acpi_madt_lapic*)hdr;

        if (!cpu_usable(lapic->flags))
            return UACPI_ITERATION_DECISION_CONTINUE;

        if (lapic->id == LAPIC_ID_NONE)
            return UACPI_ITERATION_DECISION_CONTINUE;

        ctx->has_valid_lapic_entries = true;
        return UACPI_ITERATION_DECISION_BREAK;
    }

    return UACPI_ITERATION_DECISION_CONTINUE;
}

static INIT_CODE void register_apic_id(u32 apic_id)
{
    u32 i;

    // Don't allow registering the BSP if it's not also the boot CPU
    if (unlikely(g_bsp_apic_id != g_boot_cpu_apic_id &&
                 apic_id == g_bsp_apic_id))
        return;

    for (i = 0; i < g_num_present_cpus; ++i) {
        // APIC has been registered before
        if (s_early_cpu_to_apic_id[i] == apic_id)
            return;
    }

    s_early_cpu_to_apic_id[g_num_present_cpus] = apic_id;
    pr_info(
        "registered CPU%u/0x%08X%s%s\n", g_num_present_cpus, apic_id,
        apic_id == g_boot_cpu_apic_id ? " [BOOT CPU]" : "",
        apic_id == g_bsp_apic_id ? " [BSP]" : ""
    );

    if (apic_id == g_boot_cpu_apic_id)
        g_num_online_cpus = 1;
    g_num_present_cpus++;
}

static INIT_CODE error_t register_cpu(u32 apic_id)
{
    /*
     * This is the first CPU to be registered, we expect that it's the BSP since
     * that's what the ACPI specification says.
     */
    if (g_num_online_cpus == 0) {
        /*
         * We might not know the BSP APIC ID in case we weren't booted on the
         * BSP. In that case trust ACPI enumeration.
         */
        if (unlikely(g_bsp_apic_id == APIC_ID_NONE)) {
            /*
             * We know for sure this is not the BSP since it wasn't marked as
             * such in the APIC base MSR, yet it's specified as the first CPU
             * in MADT. We have no way to determine the real BSP, disable SMP
             * completely to be safe.
             */
            if (unlikely(apic_id == g_boot_cpu_apic_id)) {
                pr_err(
                    "Unable to determine the BSP CPU, SMP won't be usable\n"
                );
                register_apic_id(g_boot_cpu_apic_id);
                return EINVAL;
            }

            pr_info("assuming BSP is APIC ID 0x%08X\n", apic_id);
            g_bsp_apic_id = apic_id;

            /*
             * We cannot use the real BSP CPU at all, since sending INIT to it
             * will reset the entire system. Just ignore it in this case.
             */
            pr_warn(
                "BSP CPU (0x%08X) will not be usable since it's not the "
                "boot CPU (0x%08X)\n", g_bsp_apic_id, g_boot_cpu_apic_id
            );

            /*
             * Mark the actual boot CPU as CPU0, we will try to re-register it
             * again once its LAPIC entry is reached in MADT, but
             * register_apic_id() should take care of de-duplication.
             */
            register_apic_id(g_boot_cpu_apic_id);
            return EOK;
        }

        if (unlikely(g_bsp_apic_id != apic_id)) {
            pr_warn(
                "Incorrect CPU (0x%08X) specified as the BSP in MADT "
                "(real BSP is 0x%08X)\n", apic_id, g_bsp_apic_id
            );

            /*
             * Just register both CPUs, but keep the invariant that CPU0 is the
             * boot CPU. The correct BSP will be de-duplicated by
             * register_apic_id().
             */
            register_apic_id(g_bsp_apic_id);
            register_apic_id(apic_id);
        }

    }

    if (g_num_present_cpus >= ULTRA_MAX_CPUS) {
        pr_warn(
            "Skipping CPU 0x%08X, configured MAX_CPUS is 0x%08X\n",
            apic_id, ULTRA_MAX_CPUS
        );

        return EOK;
    }

    register_apic_id(apic_id);
    return EOK;
}

static INIT_CODE uacpi_iteration_decision detect_lapics(
    uacpi_handle user, struct acpi_entry_hdr *hdr
)
{
    struct madt_iter_ctx *ctx = user;
    u32 apic_id;
    u32 flags;

    switch (hdr->type) {
    case ACPI_MADT_ENTRY_TYPE_LAPIC: {
        struct acpi_madt_lapic *lapic = (struct acpi_madt_lapic*)hdr;

        if (lapic->id == LAPIC_ID_NONE)
            return UACPI_ITERATION_DECISION_CONTINUE;

        flags = lapic->flags;
        apic_id = lapic->id;
        break;
    }

    case ACPI_MADT_ENTRY_TYPE_LOCAL_X2APIC: {
        struct acpi_madt_x2apic *x2apic = (struct acpi_madt_x2apic*)hdr;

        if (ctx->has_valid_lapic_entries && x2apic->id < 0xFF)
            return UACPI_ITERATION_DECISION_CONTINUE;

        if (x2apic->id == X2APIC_ID_NONE)
            return UACPI_ITERATION_DECISION_CONTINUE;

        flags = x2apic->flags;
        apic_id = x2apic->id;
        break;
    }

    case ACPI_MADT_ENTRY_TYPE_LSAPIC:
        /*
         * Apparently these might exist even on x86, but there's no confirmation
         * of that ever happening, so guard against that explicitly.
         */
        panic("Local SAPIC entries are not supported\n");

    default:
        return UACPI_ITERATION_DECISION_CONTINUE;
    }

    if (cpu_usable(flags)) {
        error_t ret;

        ret = register_cpu(apic_id);
        if (is_error(ret))
            return UACPI_ITERATION_DECISION_BREAK;
    }

    return UACPI_ITERATION_DECISION_CONTINUE;
}

static INIT_CODE uacpi_iteration_decision detect_ioapics(
    uacpi_handle user, struct acpi_entry_hdr *hdr
)
{
    UNREFERENCED_PARAMETER(user);
    struct acpi_madt_ioapic *ioapic;

    if (hdr->type != ACPI_MADT_ENTRY_TYPE_IOAPIC)
        return UACPI_ITERATION_DECISION_CONTINUE;

    ioapic = (struct acpi_madt_ioapic*)hdr;
    ioapic_register(ioapic->id, ioapic->address, ioapic->gsi_base);
    return UACPI_ITERATION_DECISION_CONTINUE;
}

static INIT_CODE uacpi_iteration_decision detect_isos(
    uacpi_handle user, struct acpi_entry_hdr *hdr
)
{
    UNREFERENCED_PARAMETER(user);
    struct acpi_madt_interrupt_source_override *iso;

    if (hdr->type != ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE)
        return UACPI_ITERATION_DECISION_CONTINUE;

    iso = (struct acpi_madt_interrupt_source_override*)hdr;

    if (unlikely(iso->bus != 0)) {
        pr_warn(
            "ignoring MADT IRQ %d override for a non-ISA bus %d\n",
            iso->source, iso->bus
        );
        return UACPI_ITERATION_DECISION_CONTINUE;
    }

    if (unlikely(iso->source > NUM_ISA_IRQS)) {
        pr_warn(
            "MADT IRQ %d override outside of ISA range, ignored\n",
            iso->source
        );
        return UACPI_ITERATION_DECISION_CONTINUE;
    }

    ioapic_register_isa_irq_override(
        iso->source, iso->gsi,
        iso->flags & ACPI_MADT_POLARITY_MASK,
        iso->flags & ACPI_MADT_TRIGGERING_MASK
    );
    return UACPI_ITERATION_DECISION_CONTINUE;
}

static INIT_CODE void setup_online_capable_bit(void)
{
    struct acpi_fadt *fadt;
    uacpi_status st;

    st = uacpi_table_fadt(&fadt);
    BUG_ON(st != UACPI_STATUS_OK);

    /*
     * Online capable bit was introduced in ACPI 6.3, prior revisions might set
     * it erroneously so don't look at it for those versions.
     */
    s_online_capable_bit_usable =
        (fadt->hdr.revision > 6) ||
        (fadt->hdr.revision == 6 && fadt->fadt_minor_verison >= 3);
}

void INIT_CODE setup_smp_topology(void)
{
    uacpi_status st;
    uacpi_table tbl;
    struct madt_iter_ctx ctx = { 0 };

    setup_online_capable_bit();

    st = uacpi_table_find_by_signature(ACPI_MADT_SIGNATURE, &tbl);
    BUG_ON(st != UACPI_STATUS_OK);

    /*
     * If MADT contains LAPIC entries we must prioritize them over x2APIC
     * entries for ids that are less than 255. However, LAPIC entries might not
     * be first in the MADT subtable list, so check beforehand.
     */
    st = uacpi_for_each_subtable(
        tbl.hdr, sizeof(struct acpi_madt), check_if_madt_has_lapic, &ctx
    );
    BUG_ON(st != UACPI_STATUS_OK);

    st = uacpi_for_each_subtable(
        tbl.hdr, sizeof(struct acpi_madt), detect_lapics, &ctx
    );
    BUG_ON(st != UACPI_STATUS_OK);
    BUG_ON(g_num_present_cpus == 0 || g_num_online_cpus == 0);

    pr_info(
        "detected %u present CPU%s\n",
        g_num_present_cpus, g_num_present_cpus == 1 ? "" : "s"
    );

    st = uacpi_for_each_subtable(
        tbl.hdr, sizeof(struct acpi_madt), detect_ioapics, &ctx
    );
    BUG_ON(st != UACPI_STATUS_OK);

    st = uacpi_for_each_subtable(
        tbl.hdr, sizeof(struct acpi_madt), detect_isos, &ctx
    );
    BUG_ON(st != UACPI_STATUS_OK);
    ioapic_finalize_overrides();

    uacpi_table_unref(&tbl);
}

static void this_cpu_enable_per_cpu(u32 my_id)
{
    struct descriptor_ptr gdt_ptr = {
        .limit = sizeof(g_this_cpu_gdt) - 1,
        .base = (ptr_t)per_cpu_ptr(&g_this_cpu_gdt, my_id),
    };
    load_gdt(&gdt_ptr);

    wrmsr_or_die(MSR_GS_BASE, g_per_cpu_offset[my_id]);
}

void INIT_CODE arch_on_per_cpu_setup_done(void)
{
    size_t i;

    // Do the boot CPU setup here, AP CPUs call it elsewhere
    this_cpu_enable_per_cpu(0);

    for (i = 0; i < g_num_present_cpus; ++i) {
        per_cpu(g_this_cpu_id, i) = i;
        per_cpu(g_this_cpu_apic_id, i) = s_early_cpu_to_apic_id[i];
    }

    /*
     * Duplicate the early boot CPU feature detection data into its own per-cpu
     * copy. From this point on g_cpu_info contains an AND mask of features of
     * all present CPUs.
     */
    memcpy(this_cpu_ptr(&g_this_cpu_info), &g_cpu_info, sizeof(g_cpu_info));
}
