#pragma once

#include <common/types.h>
#include <common/error.h>

enum io_type {
    IO_TYPE_INVALID = 0,
    IO_TYPE_PORT_IO = 1,
    IO_TYPE_MMIO = 2,
};

#ifdef ULTRA_HARDENED_IO

typedef struct io_window_impl {
    enum io_type type;
    void *address;
    size_t length;
} io_window;

#else

typedef void *io_window;

#endif
