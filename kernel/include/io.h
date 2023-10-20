#pragma once

#include <common/types.h>
#include <common/error.h>

enum io_type {
    IO_TYPE_INVALID = 0,
    IO_TYPE_PORT_IO = 1,
    IO_TYPE_MEM_IO = 2,
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

error_t io_window_map(io_window*, phys_addr_t phys_base, size_t length);
error_t io_window_map_pio(io_window*, phys_addr_t phys_base, size_t length);

void io_window_unmap(io_window*);

#define IO_FN_DECL(width)                                                   \
    u##width ioread##width##_at(io_window iow, size_t offset);              \
    void ioread##width##_many(io_window iow, size_t offset,                 \
                              u##width *buf, size_t count);                 \
                                                                            \
    static inline u##width ioread##width(io_window iow)                     \
    {                                                                       \
        return ioread##width##_at(iow, 0);                                  \
    }                                                                       \
                                                                            \
    void iowrite##width##_at(io_window iow, size_t offset, u##width value); \
    void iowrite##width##_many(io_window iow, size_t offset,                \
                               const u##width *buf, size_t count);          \
                                                                            \
    static inline void iowrite##width(io_window iow, u##width value)        \
    {                                                                       \
        iowrite##width##_at(iow, 0, value);                                 \
    }

IO_FN_DECL(8)
IO_FN_DECL(16)
IO_FN_DECL(32)

#if ULTRA_ARCH_WIDTH >= 8
IO_FN_DECL(64)
#endif

#undef IO_FN_DECL
