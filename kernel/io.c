#include <bug.h>
#include <io.h>
#include <switch.h>
#include <arch/io_types.h>
#include <private/arch/io.h>

error_t io_window_map(io_window *iow, phys_addr_t phys_base, size_t length)
{
    ptr_or_error_t ret;

    ret = arch_map_memory_io(phys_base, length);
    if (error_ptr(ret))
        return decode_error_ptr(ret);

#ifdef ULTRA_HARDENED_IO
    iow->type = IO_TYPE_MEM_IO;
    iow->address = ret;
    iow->length = length;
#else
    *iow = ret;
#endif
    return EOK;
}

error_t io_window_map_pio(io_window *iow, phys_addr_t phys_base, size_t length)
{
    pio_addr_or_error_t ret;

    if ((phys_base + ULTRA_ARCH_PORT_IO_WINDOW_OFFSET) >=
        ULTRA_ARCH_PORT_IO_WINDOW_END)
        return EINVAL;

    ret = arch_map_port_io(phys_base, length);
    if (error_pio_addr(ret))
        return decode_error_pio_addr(ret);

    ret += ULTRA_ARCH_PORT_IO_WINDOW_OFFSET;

#ifdef ULTRA_HARDENED_IO
    iow->type = IO_TYPE_PORT_IO;
    iow->address = (void*)((ptr_t)ret);
    iow->length = length;
#else
    *iow = (void*)((ptr_t)ret);
#endif
    return EOK;
}

void io_window_unmap(io_window *iow)
{
#ifdef ULTRA_HARDENED_IO
    iow->type = IO_TYPE_INVALID;
#else
    *iow = (void*)0xACACCACA;
#endif
}

static enum io_type io_window_get_type(io_window *iow)
{
#ifndef ULTRA_HARDENED_IO
    ptr_t raw_iow = (ptr_t)*iow;

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
    return *iow;
#endif
}

static void io_window_check_bounds(io_window *iow, size_t offset)
{
#ifdef ULTRA_HARDENED_IO
    BUG_ON(offset >= iow->length);
#else
    /*
     * This could technically check that *iow + offset doesn't overflow or
     * is within a certain range that we could reserve for such mappings.
     * But for now let's just leave all the checking to kernels compiled
     * with ULTRA_HARDENED_IO.
     */
    UNUSED(iow);
    UNUSED(offset);
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

#define MAKE_IO_FN(width)                                                  \
    u##width ioread##width##_at(io_window iow, size_t offset)              \
    {                                                                      \
        u##width out;                                                      \
        do_read_many(&iow, offset, sizeof(out), &out, 1);                  \
        return out;                                                        \
    }                                                                      \
                                                                           \
    void ioread##width##_many(io_window iow, size_t offset,                \
                              u##width *buf, size_t count)                 \
    {                                                                      \
        do_read_many(&iow, offset, width / 8, buf, count);                 \
    }                                                                      \
                                                                           \
    void iowrite##width##_at(io_window iow, size_t offset, u##width value) \
    {                                                                      \
        do_write_many(&iow, offset, sizeof(value), &value, 1);             \
    }                                                                      \
                                                                           \
    void iowrite##width##_many(io_window iow, size_t offset,               \
                               const u##width *buf, size_t count)          \
    {                                                                      \
        do_write_many(&iow, offset, width / 8, buf, count);                \
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
