#pragma once

#include <common/types.h>
#include <common/error.h>

extern ptr_t g_direct_map_base;

static inline phys_addr_t virt_to_phys(void *virt)
{
    return (ptr_t)virt - g_direct_map_base;
}

static inline void *phys_to_virt(phys_addr_t phys)
{
    return (void*)(phys + g_direct_map_base);
}

typedef struct io_window {
    union {
        void *mmio_address;
        ptr_t port_address;
    };

#ifdef ULTRA_HARDENED_IO
    enum io_type type;
    size_t length;
#endif

} io_window;

error_t io_window_map(io_window*, phys_addr_t phys_base, size_t length);
error_t io_window_map_pio(io_window*, phys_addr_t phys_base, size_t length);

void io_window_unmap(io_window*);

#define IO_FN_DECL(width)                                                    \
    u##width ioread##width##_at(io_window iow, size_t offset);               \
    void ioread##width##_many(io_window iow, size_t offset,                  \
                              u##width *buf, size_t count);                  \
                                                                             \
    static inline u##width ioread##width(io_window iow)                      \
    {                                                                        \
        return ioread##width##_at(iow, 0);                                   \
    }                                                                        \
                                                                             \
    void iowrite##width##_at(io_window iow, size_t offset, u##width value);  \
    void iowrite##width##_many(io_window iow, size_t offset,                 \
                               const u##width *buf, size_t count);           \
                                                                             \
    static inline void iowrite##width(io_window iow, u##width value)         \
    {                                                                        \
        iowrite##width##_at(iow, 0, value);                                  \
    }

IO_FN_DECL(8)
IO_FN_DECL(16)
IO_FN_DECL(32)

#if ULTRA_ARCH_WIDTH >= 8
IO_FN_DECL(64)
#endif

#undef IO_FN_DECL
