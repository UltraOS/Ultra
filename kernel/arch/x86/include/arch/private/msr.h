#pragma once

#include <common/types.h>
#include <common/error.h>

#define MSR_FS_BASE 0xC000'0100
#define MSR_GS_BASE 0xC000'0101
#define MSR_KERNEL_GS_BASE 0xC000'0102
#define MSR_IA32_APIC_BASE  0x0000'001B

error_t rdmsr(u32 msr, u64 *out_value);
error_t wrmsr(u32 msr, u64 value);

u64 rdmsr_or_die(u32 msr);
void wrmsr_or_die(u32 msr, u64 value);

void rdmsr_unsafe(u32 msr, u64 *out_value);
void wrmsr_unsafe(u32 msr, u64 value);
