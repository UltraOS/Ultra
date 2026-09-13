#pragma once

#include <common/types.h>
#include <common/error.h>
#include <pci/config_space.h>

/*
 * Configuration mechanism 1 over the CF8 port pair. It reaches segment 0
 * and the conventional 256 bytes only, anything beyond is ENXIO. The width
 * is the access size in bytes, 1, 2 or 4.
 */
error_t pci_type1_read(struct pci_address, u16 reg, u8 width, u32*);
error_t pci_type1_write(struct pci_address, u16 reg, u8 width, u32 value);
