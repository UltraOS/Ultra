#define MSG_FMT(msg) "xapic: " msg

#include <arch/private/apic.h>
#include <arch/private/msr.h>
#include <arch/constants.h>

#include <common/bit.h>

#include <memory/io.h>

#include <log.h>
#include <bug.h>
#include <free_after_init.h>

static u64 s_xapic_phys_base;
static io_window s_xapic_io;

static u32 xapic_read(enum apic_reg reg)
{
    return ioread32_at(s_xapic_io, (u32)reg);
}

static void xapic_write(enum apic_reg reg, u32 data)
{
    iowrite32_at(s_xapic_io, (u32)reg, data);
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
};
