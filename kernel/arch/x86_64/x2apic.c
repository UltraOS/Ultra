#define MSG_FMT(msg) "x2apic: " msg

#include <arch/private/apic.h>
#include <arch/private/msr.h>
#include <arch/constants.h>

#include <common/bit.h>

#include <memory/io.h>

#include <log.h>
#include <panic.h>
#include <free_after_init.h>

#define X2APIC_BASE_MSR 0x800

static u32 x2apic_reg_to_offset(enum apic_reg reg)
{
    return X2APIC_BASE_MSR + ((u32)reg >> 4);
}

static u32 x2apic_read(enum apic_reg reg)
{
    return rdmsr_or_die(x2apic_reg_to_offset(reg));
}

static void x2apic_write(enum apic_reg reg, u32 data)
{
    wrmsr_or_die(x2apic_reg_to_offset(reg), data);
}

// A single 64-bit write, no busy bit or separate destination half
static void x2apic_icr_write(u32 value, u32 dest_apic_id)
{
    u64 icr;

    // Older stores must be visible before the target can observe the IPI
    weak_wrmsr_fence();

    icr = ((u64)dest_apic_id << 32) | value;
    wrmsr_or_die(x2apic_reg_to_offset(APIC_REG_ICR), icr);
}

static void x2apic_send_ipi_self(u8 vector)
{
    x2apic_write(APIC_REG_SELF_IPI, vector);
}

static void INIT_CODE x2apic_setup(void)
{
    u64 apic_base_msr;

    apic_base_msr = rdmsr_or_die(MSR_IA32_APIC_BASE);

    // Enable bit is guaranteed set at this point
    if (!(apic_base_msr & IA32_APIC_BASE_X2APIC)) {
        apic_base_msr |= IA32_APIC_BASE_X2APIC;
        wrmsr_or_die(MSR_IA32_APIC_BASE, apic_base_msr);
    }
}

struct apic DATA_REFERENCES_INIT_DATA g_x2apic = {
    .read = x2apic_read,
    .write = x2apic_write,
    .setup = x2apic_setup,

    .icr_write = x2apic_icr_write,
    .send_ipi_self = x2apic_send_ipi_self,
};
