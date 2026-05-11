#pragma once

#include <common/attributes.h>
#include <common/bit.h>

#define X86_CR0_PE BIT(0)
#define X86_CR0_MP BIT(1)
#define X86_CR0_EM BIT(2)
#define X86_CR0_TS BIT(3)
#define X86_CR0_ET BIT(4)
#define X86_CR0_NE BIT(5)
#define X86_CR0_WP BIT(16)
#define X86_CR0_AM BIT(18)
#define X86_CR0_NW BIT(29)
#define X86_CR0_CD BIT(30)
#define X86_CR0_PG BIT(31)

void cr0_setup(void);

#define X86_CR4_VME BIT(0)
#define X86_CR4_PVI BIT(1)
#define X86_CR4_TSD BIT(2)
#define X86_CR4_DE BIT(3)
#define X86_CR4_PSE BIT(4)
#define X86_CR4_PAE BIT(5)
#define X86_CR4_MCE BIT(6)
#define X86_CR4_PGE BIT(7)
#define X86_CR4_PCE BIT(8)
#define X86_CR4_OSFXSR BIT(9)
#define X86_CR4_OSXMMEXCP BIT(10)
#define X86_CR4_UMIP BIT(11)
#define X86_CR4_LA57 BIT(12)
#define X86_CR4_VMXE BIT(13)
#define X86_CR4_SMXE BIT(14)
#define X86_CR4_FSGSBAS BIT(16)
#define X86_CR4_PCIDE BIT(17)
#define X86_CR4_OSXSAVE BIT(18)
#define X86_CR4_KL BIT(19)
#define X86_CR4_SMEP BIT(20)
#define X86_CR4_SMAP BIT(21)
#define X86_CR4_PKE BIT(22)
#define X86_CR4_CET BIT(23)
#define X86_CR4_PKS BIT(24)
#define X86_CR4_UINTR BIT(25)
#define X86_CR4_LASS BIT(27)
#define X86_CR4_LAM_SUP BIT(28)
#define X86_CR4_FRED BIT(32)

void cr4_feature_enable(reg_t mask);

void cr3_write(reg_t value);
