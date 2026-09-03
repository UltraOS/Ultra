#define MSG_FMT(msg) "xapic: " msg

#include <arch/private/apic.h>
#include <arch/private/msr.h>
#include <arch/constants.h>
#include <arch/cpu_helpers.h>

#include <irq_helpers.h>

#include <common/bit.h>

#include <memory/io.h>

#include <log.h>
#include <bug.h>
#include <free_after_init.h>

static u64 s_xapic_phys_base;
static io_window s_xapic_io;

static u32 xapic_read(enum apic_reg reg)
{
    return ioread32(&s_xapic_io, (u32)reg);
}

static void xapic_write(enum apic_reg reg, u32 data)
{
    iowrite32(&s_xapic_io, (u32)reg, data);
}

// A new command written while one is still being sent is dropped
static void xapic_wait_icr_idle(void)
{
    while (xapic_read(APIC_REG_ICR) & APIC_ICR_BUSY)
        arch_cpu_relax();
}

static void xapic_icr_write(u32 value, u32 dest_apic_id)
{
    irq_state_t irq_state;

    /*
     * Both halves must reach the same APIC back to back, neither a
     * migration nor an IPI sent from an interrupt may come between.
     */
    irq_state = irq_state_save();

    xapic_wait_icr_idle();

    // The destination half must be programmed first, ICR sends
    xapic_write(APIC_REG_ICR2, dest_apic_id << 24);
    xapic_write(APIC_REG_ICR, value);

    irq_state_restore(irq_state);
}

static void xapic_send_ipi_self(u8 vector)
{
    xapic_wait_icr_idle();
    xapic_write(
        APIC_REG_ICR,
        APIC_ICR_DEST_SELF | APIC_ICR_DELIVERY_FIXED | vector
    );
}

static void INIT_CODE xapic_setup(void)
{
    error_t ret;
    u64 apic_base_msr;

    apic_base_msr = rdmsr_or_die(MSR_IA32_APIC_BASE);
    s_xapic_phys_base = apic_base_msr & IA32_APIC_BASE_ADDRESS_MASK;

    pr_info("base set at 0x%016llX\n", s_xapic_phys_base);

    ret = io_window_map(&s_xapic_io, s_xapic_phys_base, PAGE_SIZE);
    BUG_ON(ret != EOK);
}

struct apic DATA_REFERENCES_INIT_DATA g_xapic = {
    .setup = xapic_setup,

    .read = xapic_read,
    .write = xapic_write,

    .icr_write = xapic_icr_write,
    .send_ipi_self = xapic_send_ipi_self,
};
