#pragma once

#include <arch/private/linker.h>

#include <common/attributes.h>
#include <common/error.h>
#include <common/helpers.h>
#include <common/bit.h>

#include <config.h>

#define HYPERVISOR \
    SECTION_VAR(HYPERVISORS_SECTION, static const, struct hypervisor)

#define BASE_HYPERVISOR_LEAF 0x40000000
#define MAX_HYPERVISOR_LEAF 0x4000FFFF

enum hypervisor_type : u8 {
    HYPERVISOR_TYPE_OTHER = 0,
    HYPERVISOR_TYPE_KVM,
    HYPERVISOR_TYPE_VMWARE,
    HYPERVISOR_TYPE_HYPERV,
    HYPERVISOR_TYPE_BHYVE,
    HYPERVISOR_TYPE_QEMU_TCG,
    HYPERVISOR_TYPE_VIRTUAL_BOX,
    HYPERVISOR_TYPE_BOCHS,
    HYPERVISOR_TYPE_UNKNOWN,
};

enum hypervisor_feature : reg_t {
    /*
     * Expands the maximum APIC ID that can be used for MSIs without IRQ
     * remapping via IOMMU to 32k.
     */
    HYPERVISOR_FEATURE_MSI_EXT_DEST_ID = BIT(0),

    // No delay is needed between consecutive PIO accesses
    HYPERVISOR_FEATURE_SKIP_PORT_IO_DELAY = BIT(1),
};

struct active_hypervisor;

struct hypervisor {
    // Name of this hypervisor for pretty-printing
    const char *name;

    // The type or flavor of this hypervisor
    enum hypervisor_type type;

    // Signature of this hypervisor if cpuid detection is supported
    char cpuid_signature[13];

    /*
     * Minimum number of leaves acceptable for this hypervisor if cpuid
     * detection is supported.
     */
    u32 min_num_leaves;

    /*
     * Maximum hypervisor leaf to scan to detect this hypervisor.
     * Acceptable values:
     * - 0 if this hypervisor is not possible to discover via cpuid probing
     * - any value >= BASE_HYPERVISOR_LEAF if this hypervisor can be
     *   discovered at any leaf between BASE_HYPERVISOR_LEAF..N
     *   in 256 increments
     */
    u32 max_leaf;

    /*
     * Optional hypervisor-specific detect callback if this hypervisor
     * can't/couldn't be discovered using cpuid probing.
     *
     * The return value indicates the "priority" and is one of:
     * - 0 if not detected (or in case of an error/missing features)
     * - 1 or any other value to indicate a desired priority value, note that
     *   it will be compared against CPUID_BASE of other hypervisors if they
     *   are detected as well.
     *
     * The hypervisor with the highest "priority" value is chosen.
     */
    u32 (*detect)(void);

    /*
     * This callback is invoked once this hypervisor is detected, it is
     * expected to do the internal initialization required and set the
     * hv->features field according to supported features
     */
    void (*setup)(struct active_hypervisor *hv);
};

struct active_hypervisor {
    struct hypervisor ops;
    enum hypervisor_feature features;

    u32 cpuid_base;
    u32 max_leaf;
    bool present;
};

#if IS_ENABLED(PARAVIRT)

void x86_detect_hypervisor(void);

// Checks if we're currently running inside a hypervisor
bool in_hypervisor(void);

// Checks if the current hypervisor supports a given feature mask
bool hypervisor_supports(enum hypervisor_feature features);

// Check if the current hypervisor is 'type'
bool hypervisor_is(enum hypervisor_type type);

#else

static inline void x86_detect_hypervisor(void) { }
static inline bool in_hypervisor(void) { return false; }

static inline bool hypervisor_supports(enum hypervisor_feature features)
{
    UNREFERENCED_PARAMETER(features);
    return false;
}

static inline bool hypervisor_is(enum hypervisor_type type)
{
    UNREFERENCED_PARAMETER(type);
    return false;
}

#endif
