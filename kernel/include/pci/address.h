#pragma once

#include <common/types.h>
#include <common/error.h>
#include <common/string_container.h>

#define PCI_DEVICES_PER_BUS 32
#define PCI_FUNCTIONS_PER_DEVICE 8

struct pci_address {
    u16 segment;
    u8 bus;
    u8 device;
    u8 function;
};
