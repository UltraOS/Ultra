#pragma once

#include <common/types.h>
#include <common/error.h>

#define ULTRA_ARCH_PORT_IO_WINDOW_OFFSET 0x10000
#define ULTRA_ARCH_PORT_IO_WINDOW_LEN 0xFFFF
#define ULTRA_ARCH_PORT_IO_WINDOW_END \
    (ULTRA_ARCH_PORT_IO_WINDOW_OFFSET + ULTRA_ARCH_PORT_IO_WINDOW_LEN + 1)

// We do a u32 here because we want to be able to encode errors
typedef u32 pio_addr_t;
typedef pio_addr_t pio_addr_or_error_t;

#define encode_error_pio_addr(ret) ((ret) + (ULTRA_ARCH_PORT_IO_WINDOW_LEN + 1))
#define decode_error_pio_addr(ret) ((ret) - (ULTRA_ARCH_PORT_IO_WINDOW_LEN + 1))
#define error_pio_addr(ret) ((ret) > ULTRA_ARCH_PORT_IO_WINDOW_LEN)
