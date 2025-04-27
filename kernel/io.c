#include <common/helpers.h>
#include <common/error.h>

#include <bug.h>
#include <io.h>
#include <switch.h>
#include <alloc.h>
#include <arch/io_types.h>

#include <private/arch/io.h>

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

typedef void io_window;

#endif

MAYBE_ERR(io_window*) io_window_map(phys_addr_t phys_base, size_t length)
{
    io_window *iow;
    ptr_or_error_t mapping;

    mapping = arch_map_memory_io(phys_base, length);
    if (error_ptr(mapping))
        return mapping;

#ifdef ULTRA_HARDENED_IO
    iow = alloc(sizeof(*iow), ALLOC_GENERIC);

    iow->type = IO_TYPE_MEM_IO;
    iow->address = mapping;
    iow->length = length;
#else
    iow = mapping;
#endif
    return iow;
}

MAYBE_ERR(io_window*) io_window_map_pio(phys_addr_t phys_base, size_t length)
{
    pio_addr_or_error_t ret;
    io_window *iow;

    if ((phys_base + ULTRA_ARCH_PORT_IO_WINDOW_OFFSET) >=
        ULTRA_ARCH_PORT_IO_WINDOW_END)
        return encode_error_ptr(EINVAL);

    ret = arch_map_port_io(phys_base, length);
    if (error_pio_addr(ret))
        return encode_error_ptr(decode_error_pio_addr(ret));

    ret += ULTRA_ARCH_PORT_IO_WINDOW_OFFSET;

#ifdef ULTRA_HARDENED_IO
    iow = alloc(sizeof(*iow), ALLOC_GENERIC);
    if (unlikely(iow == NULL))
        return encode_error_ptr(ENOMEM);

    iow->type = IO_TYPE_PORT_IO;
    iow->address = (void*)((ptr_t)ret);
    iow->length = length;
#else
    iow = (void*)((ptr_t)ret);
#endif
    return iow;
}

void io_window_unmap(io_window *iow)
{
#ifdef ULTRA_HARDENED_IO
    iow->type = IO_TYPE_INVALID;
    free(iow);
#else
    UNREFERENCED_PARAMETER(iow);
#endif
}

static enum io_type io_window_get_type(io_window *iow)
{
#ifndef ULTRA_HARDENED_IO
    ptr_t raw_iow = (ptr_t)iow;

    if (raw_iow < ULTRA_ARCH_PORT_IO_WINDOW_END) {
        if (raw_iow < ULTRA_ARCH_PORT_IO_WINDOW_OFFSET)
            return IO_TYPE_INVALID;

        return IO_TYPE_PORT_IO;
    }

    return IO_TYPE_MEM_IO;
#else
    return iow->type;
#endif
}

static void *io_window_get_address(io_window *iow)
{
#ifdef ULTRA_HARDENED_IO
    return iow->address;
#else
    return iow;
#endif
}

static void io_window_check_bounds(io_window *iow, size_t offset)
{
#ifdef ULTRA_HARDENED_IO
    BUG_ON(offset >= iow->length);
#else
    /*
     * This could technically check that iow + offset doesn't overflow or
     * is within a certain range that we could reserve for such mappings.
     * But for now let's just leave all the checking to kernels compiled
     * with ULTRA_HARDENED_IO.
     */
    UNREFERENCED_PARAMETER(iow);
    UNREFERENCED_PARAMETER(offset);
#endif
}

static pio_addr_t port_io_window_get_port(io_window *iow, size_t offset)
{
    ptr_t port = (ptr_t)io_window_get_address(iow);

    BUG_ON(port < ULTRA_ARCH_PORT_IO_WINDOW_OFFSET);
    BUG_ON(port >= ULTRA_ARCH_PORT_IO_WINDOW_END);

    io_window_check_bounds(iow, offset);

    port -= ULTRA_ARCH_PORT_IO_WINDOW_OFFSET;
    port += offset;

    return (pio_addr_t)port;
}

static void *mem_io_window_get_ptr(io_window *iow, size_t offset)
{
    void *ptr;

    io_window_check_bounds(iow, offset);

    ptr = io_window_get_address(iow);
    ptr += offset;

    return ptr;
}

static void do_write_many(io_window *iow, size_t offset, u8 width,
                          const void *in, size_t count)
{
    switch (io_window_get_type(iow)) {
    case IO_TYPE_PORT_IO: {
        pio_addr_t addr = port_io_window_get_port(iow, offset);
        arch_port_io_write(addr, width, in, count);
        break;
    } case IO_TYPE_MEM_IO: {
        void *addr = mem_io_window_get_ptr(iow, offset);
        arch_memory_io_write(addr, width, in, count);
        break;
    } default:
        BUG();
    }
}

static void do_read_many(io_window *iow, size_t offset, u8 width,
                         void *out, size_t count)
{
    switch (io_window_get_type(iow)) {
    case IO_TYPE_PORT_IO: {
        pio_addr_t addr = port_io_window_get_port(iow, offset);
        arch_port_io_read(addr, width, out, count);
        break;
    } case IO_TYPE_MEM_IO: {
        void *addr = mem_io_window_get_ptr(iow, offset);
        arch_memory_io_read(addr, width, out, count);
        break;
    } default:
        BUG();
    }
}

#define MAKE_IO_FN(width)                                                   \
    u##width ioread##width##_at(io_window *iow, size_t offset)              \
    {                                                                       \
        u##width out;                                                       \
        do_read_many(iow, offset, sizeof(out), &out, 1);                    \
        return out;                                                         \
    }                                                                       \
                                                                            \
    void ioread##width##_many(io_window *iow, size_t offset,                \
                              u##width *buf, size_t count)                  \
    {                                                                       \
        do_read_many(iow, offset, width / 8, buf, count);                   \
    }                                                                       \
                                                                            \
    void iowrite##width##_at(io_window *iow, size_t offset, u##width value) \
    {                                                                       \
        do_write_many(iow, offset, sizeof(value), &value, 1);               \
    }                                                                       \
                                                                            \
    void iowrite##width##_many(io_window *iow, size_t offset,               \
                               const u##width *buf, size_t count)           \
    {                                                                       \
        do_write_many(iow, offset, width / 8, buf, count);                  \
    }

MAKE_IO_FN(8)
MAKE_IO_FN(16)
MAKE_IO_FN(32)

#if ULTRA_ARCH_WIDTH == 8
MAKE_IO_FN(64)
#endif

/*
 * NOTE:
 * Below are generic implementations that can be overridden by
 * an architecture if the need arises.
 */

WEAK
void arch_memory_io_write(void *ptr, u8 width, const void *in, size_t count)
{
    size_t i;

#define WIDTH_SWITCH_ACTION(bits)                           \
        *((volatile u##bits*)ptr) = *(volatile u##bits*)in; \
        break;
#define WIDTH_SWITCH_ACTION_8(bits) WIDTH_SWITCH_ACTION(bits)

    for (i = 0; i < count; ++i, in += width) {
        WIDTH_SWITCH(width)
    }

#undef WIDTH_SWITCH_ACTION_8
#undef WIDTH_SWITCH_ACTION
}

WEAK
void arch_port_io_write(pio_addr_t addr, u8 width, const void *in, size_t count)
{
    arch_memory_io_write((void*)((ptr_t)addr), width, in, count);
}

WEAK
void arch_memory_io_read(void *ptr, u8 width, void *out, size_t count)
{
    size_t i;

#define WIDTH_SWITCH_ACTION(bits)                        \
        *((volatile u##bits*)out) = *(volatile u##bits*)ptr; \
        break;
#define WIDTH_SWITCH_ACTION_8(bits) WIDTH_SWITCH_ACTION(bits)

    for (i = 0; i < count; ++i, out += width) {
        WIDTH_SWITCH(width)
    }

#undef WIDTH_SWITCH_ACTION_8
#undef WIDTH_SWITCH_ACTION
}

WEAK
void arch_port_io_read(pio_addr_t addr, u8 width, void *out, size_t count)
{
    arch_memory_io_read((void*)((ptr_t)addr), width, out, count);
}
