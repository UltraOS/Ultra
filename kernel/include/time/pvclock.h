#pragma once

#include <common/attributes.h>
#include <common/types.h>
#include <common/bit.h>
#include <common/error.h>

#include <per_cpu_decls.h>

struct pvclock_vcpu_time_info {
    u32 version;
    u32 rsvd0;
    u64 host_tsc_timestamp;
    u64 host_ns_since_boot;
    u32 guest_tsc_mul;
    i8 guest_tsc_shift;

#define PVCLOCK_TSC_STABLE BIT_U8(0)
#define PVCLOCK_GUEST_STOPPED BIT_U8(1)
    u8 flags;

    u8 rsvd[2];
};
EXPECT_SIZEOF(struct pvclock_vcpu_time_info, 32);

struct pvclock_wall_clock {
    u32 version;
    u32 sec;
    u32 nsec;
};
EXPECT_SIZEOF(struct pvclock_wall_clock, 12);

DECLARE_PER_CPU(struct pvclock_vcpu_time_info*, g_this_cpu_time_info);

void pvclock_enable_stable_bit(void);
error_t pvclock_counter_register(void);

u64 pvclock_read_from(struct pvclock_vcpu_time_info*);
#define pvclock_read() pvclock_read_from(this_cpu_read(g_this_cpu_time_info))

u64 pvclock_calculate_tsc_hz(struct pvclock_vcpu_time_info*);
