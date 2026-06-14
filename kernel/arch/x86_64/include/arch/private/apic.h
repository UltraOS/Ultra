#pragma once

#include <common/types.h>

enum apic_reg {
    APIC_REG_ID = 0x20,
    APIC_REG_VERSION = 0x30,
    APIC_REG_TPR = 0x80,
    APIC_REG_APR = 0x90,
    APIC_REG_PPR = 0xA0,
    APIC_REG_EOI = 0xB0,
    APIC_REG_REMOTE_READ = 0xC0,
    APIC_REG_LDR = 0xD0,
    APIC_REG_DFR = 0xE0,
    APIC_REG_SVR = 0xF0,

    APIC_REG_ISR = 0x100,
    APIC_REG_TMR = 0x180,
    APIC_REG_IRR = 0x200,
    APIC_REG_ESR = 0x280,

    APIC_REG_LVT_CMCI = 0x2F0,
    APIC_REG_ICR = 0x300,
    APIC_REG_ICR2 = 0x310,
    APIC_REG_LVT_TIMER = 0x320,
    APIC_REG_LVT_THERMAL = 0x330,
    APIC_REG_LVT_PERF = 0x340,
    APIC_REG_LVT_LINT0 = 0x350,
    APIC_REG_LVT_LINT1 = 0x360,
    APIC_REG_LVT_ERROR = 0x370,

    APIC_REG_TIMER_INITIAL_COUNT = 0x380,
    APIC_REG_TIMER_CURRENT_COUNT = 0x390,
    APIC_REG_TIMER_DIVIDE_CONFIG = 0x3E0,

    APIC_REG_EXT_FEATURE = 0x400,
    APIC_REG_EXT_CONTROL = 0x410,
    APIC_REG_SEOI = 0x420,
    APIC_REG_IER = 0x480,
    APIC_REG_EXT_LVT = 0x500,
};

#define APIC_ID_NONE 0xFFFF'FFFF

enum apic_mode {
    APIC_MODE_NONE = 0,
    APIC_MODE_X,
    APIC_MODE_X2,
    APIC_NUM_MODES,
};

extern struct apic *g_apic;
extern enum apic_mode g_apic_mode;

extern u32 g_bsp_apic_id;
extern u32 g_boot_cpu_apic_id;

struct apic {
    void (*setup)(void);
    u32 (*read)(enum apic_reg);
    void (*write)(enum apic_reg, u32 value);
};

void apic_detect(void);

void apic_set_known_frequency(u64 khz);
