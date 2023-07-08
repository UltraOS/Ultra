#include <bug.h>
#include <io_impl.h>

// TODO: Optimize PIO functions using the REP prefix
// TODO: Use a better approach than raw volatile accesses with casts for MMIO
// TODO: Special-case {read/write}_many for count == 1

#define IO_WINDOW_PIO_BASE 0x10000
#define IO_WINDOW_PIO_END ((IO_WINDOW_PIO_BASE) + 0xFFFF)

#define MAKE_PIO_FN(width, name)                                             \
    static inline void out##width(u16 port, u##width data)                   \
    {                                                                        \
        asm volatile("out" #name " %0, %1" ::"a"(data), "Nd"(port));         \
    }                                                                        \
                                                                             \
    static inline u##width in##width(u16 port)                               \
    {                                                                        \
        u##width out_value = 0;                                              \
        asm volatile("in" #name " %1, %0" : "=a"(out_value) : "Nd"(port) :); \
        return out_value;                                                    \
    }

MAKE_PIO_FN(8, b)
MAKE_PIO_FN(16, w)
MAKE_PIO_FN(32, l)

error_t io_window_map(io_window *iow, ptr_t phys_base, size_t length)
{
    UNUSED(iow);
    UNUSED(phys_base);
    UNUSED(length);

    return ENOTSUP;
}

#ifdef ULTRA_HARDENED_IO

error_t io_window_map_pio(io_window *iow, u16 base, u16 length)
{
    ptr_t real_base = base + IO_WINDOW_PIO_BASE;

    iow->type = IO_TYPE_PORT_IO;
    iow->address = (void*)real_base;
    iow->length = length;
    return EOK;
}

void io_window_unmap(io_window *iow)
{
    iow->type = IO_TYPE_INVALID;
}

static enum io_type io_window_get_type(io_window *iow)
{
    return iow->type;
}

static void *io_window_get_address(io_window *iow)
{
    return iow->address;
}

static void io_window_check_bounds(io_window *iow, size_t offset)
{
    BUG_ON(offset >= iow->length);
}

#else

error_t io_window_map_pio(io_window *iow, u16 base, u16 length)
{
    UNUSED(length);
    *iow = (void*)((ptr_t)base + IO_WINDOW_PIO_BASE);
    return EOK;
}

static enum io_type io_window_get_type(io_window *iow)
{
    ptr_t raw_iow = (ptr_t)*iow;

    if (raw_iow <= IO_WINDOW_PIO_END) {
        if (raw_iow < IO_WINDOW_PIO_BASE)
            return IO_TYPE_INVALID;

        return IO_TYPE_PORT_IO;
    }

    return IO_TYPE_MMIO;
}

static void *io_window_get_address(io_window *iow)
{
    return *iow;
}

void io_window_unmap(io_window *iow)
{
    *iow = (void*)0xACACCACA;
}

static void io_window_check_bounds(io_window *iow, size_t offset)
{
    /*
     * This could technically check that *iow + offset doesn't overflow or
     * is within a certain range that we could reserve for such mappings.
     * But for now let's just leave all the checking to kernels compiled
     * with ULTRA_HARDENED_IO.
     */
    UNUSED(iow);
    UNUSED(offset);
}

#endif

static u16 pio_window_get_port(io_window *iow, size_t offset)
{
    ptr_t port = (ptr_t)io_window_get_address(iow);

    BUG_ON(port < IO_WINDOW_PIO_BASE);
    BUG_ON(port > IO_WINDOW_PIO_END);

    io_window_check_bounds(iow, offset);

    port -= IO_WINDOW_PIO_BASE;
    port += offset;

    return (u16)port;
}

static void *mmio_window_get_ptr(io_window *iow, size_t offset)
{
    void *ptr;

    io_window_check_bounds(iow, offset);

    ptr = io_window_get_address(iow);
    ptr += offset;

    return ptr;
}

#if ULTRA_ARCH_WIDTH >= 8
#define WIDTH_SWITCH_8            \
    case 8:                       \
        WIDTH_SWITCH_ACTION_8(64)
#else
#define WIDTH_SWITCH_8
#endif

#define WIDTH_SWITCH(width)     \
    switch (width) {            \
    case 1:                     \
        WIDTH_SWITCH_ACTION(8)  \
    case 2:                     \
        WIDTH_SWITCH_ACTION(16) \
    case 4:                     \
        WIDTH_SWITCH_ACTION(32) \
    WIDTH_SWITCH_8              \
    default:                    \
        BUG();                  \
    }

static void do_pio_reads(io_window *iow, size_t offset, u8 width,
                         void *out, size_t count)
{
    size_t i;
    u16 port;

    port = pio_window_get_port(iow, offset);

    #define WIDTH_SWITCH_ACTION_8(bits)
    #define WIDTH_SWITCH_ACTION(bits)          \
            *((u##bits*)out) = in##bits(port); \
            break;

    for (i = 0; i < count; ++i, out += width) {
        WIDTH_SWITCH(width)
    }

    #undef WIDTH_SWITCH_ACTION
    #undef WIDTH_SWITCH_ACTION_8
}


static void do_mmio_reads(io_window *iow, size_t offset, u8 width,
                          void *out, size_t count)
{
    size_t i;
    void *ptr;

    ptr = mmio_window_get_ptr(iow, offset);

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

static void do_read_many(io_window *iow, size_t offset, u8 width,
                         void *out, size_t count)
{
    switch (io_window_get_type(iow)) {
    case IO_TYPE_PORT_IO:
        do_pio_reads(iow, offset, width, out, count);
        break;
    case IO_TYPE_MMIO:
        do_mmio_reads(iow, offset, width, out, count);
        break;
    default:
        BUG();
    }
}

static void do_pio_writes(io_window *iow, size_t offset, u8 width,
                          const void *in, size_t count)
{
    size_t i;
    u16 port;

    port = pio_window_get_port(iow, offset);

    #define WIDTH_SWITCH_ACTION_8(bits)
    #define WIDTH_SWITCH_ACTION(bits)       \
            out##bits(port, *(u##bits*)in); \
            break;

    for (i = 0; i < count; ++i, in += width) {
        WIDTH_SWITCH(width)
    }

    #undef WIDTH_SWITCH_ACTION
    #undef WIDTH_SWITCH_ACTION_8
}

static void do_mmio_writes(io_window *iow, size_t offset, u8 width,
                           const void *in, size_t count)
{
    size_t i;
    void *ptr;

    ptr = mmio_window_get_ptr(iow, offset);

    #define WIDTH_SWITCH_ACTION(bits)                       \
        *((volatile u##bits*)ptr) = *(volatile u##bits*)in; \
        break;
    #define WIDTH_SWITCH_ACTION_8(bits) WIDTH_SWITCH_ACTION(bits)

    for (i = 0; i < count; ++i, in += width) {
        WIDTH_SWITCH(width)
    }

    #undef WIDTH_SWITCH_ACTION_8
    #undef WIDTH_SWITCH_ACTION
}

static void do_write_many(io_window *iow, size_t offset, u8 width,
                          const void *in, size_t count)
{
    switch (io_window_get_type(iow)) {
    case IO_TYPE_PORT_IO:
        do_pio_writes(iow, offset, width, in, count);
        break;
    case IO_TYPE_MMIO:
        do_mmio_writes(iow, offset, width, in, count);
        break;
    default:
        BUG();
    }
}

#define MAKE_IO_FN(width)                                               \
    u##width ioread##width(io_window iow, size_t offset)                \
    {                                                                   \
        u##width out;                                                   \
        do_read_many(&iow, offset, sizeof(out), &out, 1);               \
        return out;                                                     \
    }                                                                   \
                                                                        \
    void ioread##width##_many(io_window iow, size_t offset,             \
                              u##width *buf, size_t count)              \
    {                                                                   \
        do_read_many(&iow, offset, width / 8, buf, count);              \
    }                                                                   \
                                                                        \
    void iowrite##width(io_window iow, size_t offset, u##width value)   \
    {                                                                   \
        do_write_many(&iow, offset, sizeof(value), &value, 1);          \
    }                                                                   \
                                                                        \
    void iowrite##width##_many(io_window iow, size_t offset,            \
                               const u##width *buf, size_t count)       \
    {                                                                   \
        do_write_many(&iow, offset, width / 8, buf, count);             \
    }

MAKE_IO_FN(8)
MAKE_IO_FN(16)
MAKE_IO_FN(32)

#if ULTRA_ARCH_WIDTH == 8
MAKE_IO_FN(64)
#endif
