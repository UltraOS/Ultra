#pragma once

#include <arch/private/pci_type1.h>

// Configuration space access for the time before the PCI core exists
#define EARLY_PCI_MAKE_RW(width)                                    \
    static inline error_t early_pci_read##width(                    \
        struct pci_address addr, u16 reg, u##width *out_value       \
    )                                                               \
    {                                                               \
        error_t ret;                                                \
        u32 value;                                                  \
                                                                    \
        ret = pci_type1_read(addr, reg, sizeof(u##width), &value);  \
        if (is_error(ret))                                          \
            return ret;                                             \
                                                                    \
        *out_value = value;                                         \
        return EOK;                                                 \
    }                                                               \
                                                                    \
    static inline error_t early_pci_write##width(                   \
        struct pci_address addr, u16 reg, u##width value            \
    )                                                               \
    {                                                               \
        return pci_type1_write(addr, reg, sizeof(u##width), value); \
    }

EARLY_PCI_MAKE_RW(8)
EARLY_PCI_MAKE_RW(16)
EARLY_PCI_MAKE_RW(32)
