#pragma once

#include <common/types.h>
#include <common/error.h>
#include <common/bit.h>

#include <arch/constants.h>
#include <arch/io.h>

#define MAX_PHYS_ADDR BIT_OF_TYPE(phys_addr_t, MAX_PHYS_BITS)

extern ptr_t g_direct_map_base;

static inline phys_addr_t virt_to_phys(void *virt)
{
    return (ptr_t)virt - g_direct_map_base;
}

static inline void *phys_to_virt(phys_addr_t phys)
{
    return (void*)(phys + g_direct_map_base);
}

enum io_type : u8 {
    IO_TYPE_INVALID = 0,
    IO_TYPE_PORT_IO,
    IO_TYPE_MEM_IO,
    IO_TYPE_UNMAPPED = 0xFF,
};

#ifndef ARCH_HAS_CUSTOM_PIO
typedef void *pio_addr_t;
#endif

typedef struct io_window {
    union {
        void *mmio_address;
        pio_addr_t port_address;
    };

    size_t length;
    u8 type;
} io_window;

/*
 * Helpers for mapping MMIO into the CPU address space.
 * io_window_map -> maps memory as normal uncached MMIO, applicable for
 *                  most mappings
 * io_window_map_wc -> attempts to map with write combining
 * io_window_map_wt -> attempts to map as write-through
 * io_window_map_np -> maps MMIO in non-posted (synchronous) mode
 * io_window_map_pio -> maps PIO into the CPU address space
 *                      (if applicable for the architecture)
 *
 * Use the io{read,write} helpers to access.
 */
error_t io_window_map(io_window*, phys_addr_t phys_base, size_t length);
error_t io_window_map_wc(io_window*, phys_addr_t phys_base, size_t length);
error_t io_window_map_wt(io_window*, phys_addr_t phys_base, size_t length);
error_t io_window_map_np(io_window*, phys_addr_t phys_base, size_t length);
error_t io_window_map_pio(io_window*, phys_addr_t phys_base, size_t length);

// Maps requested physical memory in cached mode
void *io_window_map_cached(phys_addr_t phys_base, size_t length);

// Retrieves the raw pointer underlying this io_window
void *io_window_raw_ptr(io_window*);

void io_window_unmap(io_window*, size_t length);
void io_window_unmap_ptr(void*, size_t length);

#define IO_FN_DECL(width, suffix)                                               \
    u##width ioread##width##suffix(io_window *iow, size_t offset);              \
    void iowrite##width##suffix(io_window *iow, size_t offset, u##width value);

#define IO_FN_MANY_DECL(width, suffix)                                     \
    void ioread##width##suffix##_many(io_window *iow, size_t offset,       \
                                      u##width *buf, size_t count);        \
    void iowrite##width##suffix##_many(io_window *iow, size_t offset,      \
                                       const u##width *buf, size_t count); \

/*
 * PIO/MMIO access helpers:
 * - io{read,write}{8,16,32,64} for generic, strongly ordered MMIO
 * - io{read,write}{8,16,32,64}_relaxed for raw, unordered MMIO
 * - io{read,write}{8,16,32}_slowdown only applicable for PIO where
 *   bus-specific delays are required to allow the device to catch up
 *
 * - arch-optimized io{read,write}{8,16,32,64}{_relaxed}_many are also
 *   available to write or read a stream of bytes in a burst
 */
IO_FN_DECL(8, )
IO_FN_DECL(8, _relaxed)
IO_FN_MANY_DECL(8, )
IO_FN_MANY_DECL(8, _relaxed)

IO_FN_DECL(16, )
IO_FN_DECL(16, _relaxed)
IO_FN_MANY_DECL(16, )
IO_FN_MANY_DECL(16, _relaxed)

IO_FN_DECL(32, )
IO_FN_DECL(32, _relaxed)
IO_FN_MANY_DECL(32, )
IO_FN_MANY_DECL(32, _relaxed)

IO_FN_DECL(8, _slowdown)
IO_FN_DECL(16, _slowdown)
IO_FN_DECL(32, _slowdown)

#if ULTRA_ARCH_WIDTH >= 8
IO_FN_DECL(64, )
IO_FN_DECL(64, _relaxed)
IO_FN_MANY_DECL(64, )
IO_FN_MANY_DECL(64, _relaxed)
#endif

#undef IO_FN_DECL
