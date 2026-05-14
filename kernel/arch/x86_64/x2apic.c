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
};
