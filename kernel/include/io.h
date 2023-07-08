#pragma once

#include <common/types.h>
#include <common/error.h>

#include <io_impl.h>

error_t io_window_map(io_window*, ptr_t phys_base, size_t length);
error_t io_window_map_pio(io_window*, u16 base, u16 length);

void io_window_unmap(io_window*);

#define IO_FN_DECL(width)                                              \
    u##width ioread##width(io_window iow, size_t offset);              \
    void ioread##width##_many(io_window iow, size_t offset,            \
                              u##width *buf, size_t count);            \
                                                                       \
    void iowrite##width(io_window iow, size_t offset, u##width value); \
    void iowrite##width##_many(io_window iow, size_t offset,           \
                               const u##width *buf, size_t count);

IO_FN_DECL(8)
IO_FN_DECL(16)
IO_FN_DECL(32)

#if ULTRA_ARCH_WIDTH >= 8
IO_FN_DECL(64)
#endif

#undef IO_FN_DECL
