#include <common/types.h>
#include <common/error.h>
#include <bug.h>
#include <switch.h>
#include <private/arch/io.h>

ptr_or_error_t arch_map_memory_io(phys_addr_t phys_base, size_t length)
{
    UNREFERENCED_PARAMETER(phys_base);
    UNREFERENCED_PARAMETER(length);
    return encode_error_ptr(ENOTSUP);
}

pio_addr_t arch_map_port_io(phys_addr_t phys_base, size_t length)
{
    UNREFERENCED_PARAMETER(length);
    return (pio_addr_t)phys_base;
}

// TODO: Optimize PIO functions using the REP prefix
// TODO: Use a better approach than raw volatile accesses with casts for MMIO
// TODO: Special-case {read/write}_many for count == 1

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

void arch_port_io_read(pio_addr_t addr, u8 width, void *out, size_t count)
{
    size_t i;
    u16 port = (u16)addr;

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

void arch_port_io_write(pio_addr_t addr, u8 width, const void *in, size_t count)
{
    size_t i;
    u16 port = (u16)addr;

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
