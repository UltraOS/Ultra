#pragma once

#include <common/types.h>
#include <common/error.h>
#include <arch/io_types.h>

ptr_or_error_t arch_map_memory_io(phys_addr_t phys_base, size_t length);
pio_addr_or_error_t arch_map_port_io(phys_addr_t phys_base, size_t length);

void arch_memory_io_write(void *addr, u8 width, const void *in, size_t count);
void arch_memory_io_read(void *addr, u8 width, void *out, size_t count);

void arch_port_io_write(pio_addr_t addr, u8 width, const void *in, size_t count);
void arch_port_io_read(pio_addr_t addr, u8 width, void *out, size_t count);
