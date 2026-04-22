#pragma once

#include <common/types.h>
#include <common/error.h>

#define ULTRA_ARCH_PORT_IO_WINDOW_OFFSET 0x1000000
#define ULTRA_ARCH_PORT_IO_WINDOW_LEN 0x1000000
#define ULTRA_ARCH_PORT_IO_WINDOW_END \
    (ULTRA_ARCH_PORT_IO_WINDOW_OFFSET + ULTRA_ARCH_PORT_IO_WINDOW_LEN + 1)

typedef void *pio_addr_t;
typedef pio_addr_t pio_addr_or_error_t;

#define encode_error_pio_addr(ret) encode_error_ptr(ret)
#define decode_error_pio_addr(ret) decode_error_ptr(ret)
#define error_pio_addr(ret) error_ptr(ret)
