#pragma once

#include <common/bit.h>

#define KVM_CPUID_FEATURES 0x40000001

// kvmclock available at msrs 0x11 and 0x12
#define KVM_FEATURE_CLOCKSOURCE BIT_U32(0)
    #define MSR_KVM_WALL_CLOCK  0x11
    #define MSR_KVM_SYSTEM_TIME 0x12

// KVM_FEATURE_NOP_IO_DELAY
#define KVM_FEATURE_NOP_IO_DELAY BIT_U32(1)

// kvmclock available at msrs 0x4b564d00 and 0x4b564d01
#define KVM_FEATURE_CLOCKSOURCE2 BIT_U32(3)
    #define MSR_KVM_WALL_CLOCK_NEW  0x4b564d00
    #define MSR_KVM_SYSTEM_TIME_NEW 0x4b564d01

// steal time can be enabled by writing to msr 0x4b564d03
#define KVM_FEATURE_STEAL_TIME BIT_U32(5)

/* 
 * paravirtualized end of interrupt handler can be enabled by
 * writing to msr 0x4b564d04
 */
#define KVM_FEATURE_PV_EOI BIT_U32(6)

/*
 * guest checks this feature bit before enabling paravirtualized spinlock
 * support
 */
#define KVM_FEATURE_PV_UNHALT BIT_U32(7)

// guest checks this feature bit before enabling paravirtualized tlb flush
#define KVM_FEATURE_PV_TLB_FLUSH BIT_U32(9)

/*
 * paravirtualized async PF VM EXIT can be enabled by setting bit 2 when
 * writing to msr 0x4b564d02
 */
#define KVM_FEATURE_ASYNC_PF_VMEXIT BIT_U32(10)

// guest checks this feature bit before enabling paravirtualized send IPIs
#define KVM_FEATURE_PV_SEND_IPI BIT_U32(11)

// host-side polling on HLT can be disabled by writing to msr 0x4b564d05
#define KVM_FEATURE_POLL_CONTROL BIT_U32(12)

// guest checks this feature bit before using paravirtualized sched yield
#define KVM_FEATURE_PV_SCHED_YIELD BIT_U32(13)

/*
 * guest checks this feature bit before using the second async pf control msr
 * 0x4b564d06 and async pf acknowledgment msr 0x4b564d07
 */
#define KVM_FEATURE_ASYNC_PF_INT BIT_U32(14)

/*
 * guest checks this feature bit before using extended destination ID bits
 * in MSI address bits 11-5
 */
#define KVM_FEATURE_MSI_EXT_DEST_ID BIT_U32(15)

/*
 * guest checks this feature bit before using the map gpa range hypercall
 * to notify the page state change
 */
#define KVM_FEATURE_HC_MAP_GPA_RANGE BIT_U32(16)

// guest checks this feature bit before using MSR_KVM_MIGRATION_CONTROL
#define KVM_FEATURE_MIGRATION_CONTROL BIT_U32(17)

// host will warn if no guest-side per-cpu warps are expected in kvmclock
#define KVM_FEATURE_CLOCKSOURCE_STABLE_BIT BIT_U32(24)

/*
 * guest checks this feature bit to determine that vCPUs are never preempted
 * for an unlimited time allowing optimizations
 */
#define KVM_HINTS_REALTIME BIT_U32(0)

/*
 * All MSR writes that set an address must also set KVM_MSR_ENABLED to indicate
 * that it's valid
 */
#define KVM_MSR_ENABLED BIT_U8(0)
