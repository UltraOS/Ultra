#pragma once

#include <common/types.h>
#include <common/helpers.h>

/*
 * API provided by <arch/detail/io.h>:
 *
 * [Optional, gated by ARCH_HAS_CUSTOM_PIO]
 *   A typedef for pio_addr_t, required if an architecture implements PIO
 *   that is not emulated over MMIO.
 */
#if HAS_INCLUDE(<arch/detail/io.h>)
#include <arch/detail/io.h>
#endif

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
