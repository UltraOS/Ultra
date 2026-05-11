#pragma once

#include <common/types.h>
#include <common/error.h>
#include <common/bit.h>

#define MSR_IA32_EFER 0xC0000080 // please use efer_feature_enable() instead
#define MSR_FS_BASE 0xC000'0100
#define MSR_GS_BASE 0xC000'0101
#define MSR_KERNEL_GS_BASE 0xC000'0102
#define MSR_IA32_APIC_BASE  0x0000'001B

// MSR_IA32_APIC_BASE
#define IA32_APIC_BASE_ADDRESS_MASK MAKE_BIT_MASK(51, 12)
#define IA32_APIC_BASE_IS_ENABLED BIT(11)
#define IA32_APIC_BASE_X2APIC BIT(10)
#define IA32_APIC_BASE_IS_BSP BIT(8)

// MSR_IA32_EFER
#define IA32_EFER_SCE BIT(0)
#define IA32_EFER_DPE BIT(1)
#define IA32_EFER_LME BIT(8)
#define IA32_EFER_LMA BIT(10)
#define IA32_EFER_NX BIT(11)
#define IA32_EFER_SVME BIT(12)
#define IA32_EFER_LMSLE BIT(13)
#define IA32_EFER_FFXSR BIT(14)
#define IA32_EFER_TCE BIT(15)
#define IA32_EFER_MCOMMIT BIT(17)
#define IA32_EFER_INTWB BIT(18)
#define IA32_EFER_AUTOIBRS BIT(21)

void efer_feature_enable(u64 mask);

error_t rdmsr(u32 msr, u64 *out_value);
error_t wrmsr(u32 msr, u64 value);

u64 rdmsr_or_die(u32 msr);
void wrmsr_or_die(u32 msr, u64 value);

void rdmsr_unsafe(u32 msr, u64 *out_value);
void wrmsr_unsafe(u32 msr, u64 value);
