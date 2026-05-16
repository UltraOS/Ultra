#pragma once

#include <common/atomic.h>
#include <arch/private/io.h>

#define X86_PORT_IO_WINDOW_OFFSET 0x10000
#define X86_PORT_IO_WINDOW_LEN 0xFFFF
#define X86_PORT_IO_WINDOW_END \
    (X86_PORT_IO_WINDOW_OFFSET + X86_PORT_IO_WINDOW_LEN + 1)

#define encode_error_pio_addr(ret) ((ret) + X86_PORT_IO_WINDOW_END)
#define decode_error_pio_addr(ret) ((ret) - X86_PORT_IO_WINDOW_END)
#define error_pio_addr(ret) ((ret) >= X86_PORT_IO_WINDOW_END)

static inline pio_addr_or_error_t arch_map_pio(
    phys_addr_t phys_base, size_t length
)
{
    if (unlikely((phys_base + length) > X86_PORT_IO_WINDOW_LEN))
        return encode_error_pio_addr(EINVAL);

    return (pio_addr_t)(X86_PORT_IO_WINDOW_OFFSET + phys_base);
}

static inline void arch_unmap_pio(phys_addr_t phys_base, size_t length)
{
    UNREFERENCED_PARAMETER(phys_base);
    UNREFERENCED_PARAMETER(length);
}

#define X86_MAKE_ARCH_PIO_RW(                                        \
    pio_suffix, io_suffix, width, fn_suffix, asm_barrier, after_code \
)                                                                    \
    static inline u##width arch_pio_read##width##fn_suffix(          \
        pio_addr_t port                                              \
    )                                                                \
    {                                                                \
        u##width val;                                                \
                                                                     \
        port -= X86_PORT_IO_WINDOW_OFFSET;                           \
        asm volatile(                                                \
            "in" pio_suffix " %w1, %" io_suffix "0"                  \
            : "=a" (val) : "Nd" (port) asm_barrier                   \
        );                                                           \
        after_code                                                   \
        return val;                                                  \
    }                                                                \
                                                                     \
    static inline void arch_pio_write##width##fn_suffix(             \
        pio_addr_t port, u##width val                                \
    )                                                                \
    {                                                                \
        port -= X86_PORT_IO_WINDOW_OFFSET;                           \
        asm volatile(                                                \
            "out" pio_suffix " %" io_suffix "0, %w1"                 \
            :: "a" (val), "Nd" (port) asm_barrier                    \
        );                                                           \
        after_code                                                   \
    }

extern bool g_can_skip_pio_delay;

static inline void x86_pio_slowdown(void)
{
    if (g_can_skip_pio_delay)
        return;

    asm volatile("outb %al, $0x80");
}

X86_MAKE_ARCH_PIO_RW("b", "b",  8, , : "memory", )
X86_MAKE_ARCH_PIO_RW("w", "w", 16, , : "memory", )
X86_MAKE_ARCH_PIO_RW("l",    , 32, , : "memory", )

X86_MAKE_ARCH_PIO_RW("b", "b",  8, _slowdown, : "memory", x86_pio_slowdown();)
X86_MAKE_ARCH_PIO_RW("w", "w", 16, _slowdown, : "memory", x86_pio_slowdown();)
X86_MAKE_ARCH_PIO_RW("l",    , 32, _slowdown, : "memory", x86_pio_slowdown();)

X86_MAKE_ARCH_PIO_RW("b", "b",  8, _relaxed, ,)
X86_MAKE_ARCH_PIO_RW("w", "w", 16, _relaxed, ,)
X86_MAKE_ARCH_PIO_RW("l",    , 32, _relaxed, ,)

#define X86_MAKE_ARCH_PIO_RW_MANY(pio_suffix, width, fn_suffix)     \
    static inline void arch_pio_read##width##fn_suffix##_many(      \
        pio_addr_t port, u##width *buf, size_t count                \
    )                                                               \
    {                                                               \
        port -= X86_PORT_IO_WINDOW_OFFSET;                          \
        asm volatile(                                               \
            "rep ins" pio_suffix                                    \
            : "+D" (buf), "+c" (count) : "d" ((u16)port) : "memory" \
        );                                                          \
    }                                                               \
                                                                    \
    static inline void arch_pio_write##width##fn_suffix##_many(     \
        pio_addr_t port, const u##width *buf, size_t count          \
    )                                                               \
    {                                                               \
        port -= X86_PORT_IO_WINDOW_OFFSET;                          \
        asm volatile(                                               \
            "rep outs" pio_suffix                                   \
            : "+S" (buf), "+c" (count) : "d" ((u16)port) : "memory" \
        );                                                          \
    }

X86_MAKE_ARCH_PIO_RW_MANY("b",  8, )
X86_MAKE_ARCH_PIO_RW_MANY("w", 16, )
X86_MAKE_ARCH_PIO_RW_MANY("l", 32, )

X86_MAKE_ARCH_PIO_RW_MANY("b",  8, _relaxed)
X86_MAKE_ARCH_PIO_RW_MANY("w", 16, _relaxed)
X86_MAKE_ARCH_PIO_RW_MANY("l", 32, _relaxed)

/*
 * Force AL/AX/EAX/RAX for MMIO because of a quirk from old AMD CPUs.
 *
 * Excerpt from
 * "BIOS and Kernel Developer’s Guide (BKDG) For AMD Family 10h Processors"
 *
 * 2.11.1 MMIO Configuration Coding Requirements
 *
 * Instructions used to read MMIO configuration space are required to take
 * the following form:
 *
 *    mov eax/ax/al, <any_address_mode>;
 *
 * Instructions used to write MMIO configuration space are required to take
 * the following form:
 *
 *     mov <any_address_mode>, eax/ax/al;
 *
 * No other source/target registers may be use other than eax/ax/al.
 */
#define X86_MAKE_ARCH_MMIO_RW(                                         \
    reg_name, mov_suffix, width, fn_suffix, asm_barrier                \
)                                                                      \
    static inline u##width arch_mmio_read##width##fn_suffix(void *ptr) \
    {                                                                  \
        u##width val;                                                  \
        asm volatile(                                                  \
            "mov" mov_suffix " %1, %%" reg_name                        \
            : "=a" (val) : "m" (*(volatile u##width*)ptr) asm_barrier  \
        );                                                             \
        return val;                                                    \
    }                                                                  \
                                                                       \
    static inline void arch_mmio_write##width##fn_suffix(              \
        void *ptr, u##width val                                        \
    )                                                                  \
    {                                                                  \
        asm volatile(                                                  \
            "mov" mov_suffix " %%" reg_name ", %1"                     \
            :: "a" (val), "m" (*(volatile u##width*)ptr) asm_barrier   \
        );                                                             \
    }

X86_MAKE_ARCH_MMIO_RW("al",  "b",  8, , : "memory")
X86_MAKE_ARCH_MMIO_RW("ax",  "w", 16, , : "memory")
X86_MAKE_ARCH_MMIO_RW("eax", "l", 32, , : "memory")
X86_MAKE_ARCH_MMIO_RW("rax", "q", 64, , : "memory")

X86_MAKE_ARCH_MMIO_RW("al",  "b",  8, _relaxed, )
X86_MAKE_ARCH_MMIO_RW("ax",  "w", 16, _relaxed, )
X86_MAKE_ARCH_MMIO_RW("eax", "l", 32, _relaxed, )
X86_MAKE_ARCH_MMIO_RW("rax", "q", 64, _relaxed, )

#define ARCH_HAS_CUSTOM_MMIO_RW_MANY_STRICT
#define X86_MAKE_MMIO_RW_MANY_STRICT(width)                \
    static inline void arch_mmio_write##width##_many(      \
        void *ptr, const u##width *buf, size_t size        \
    )                                                      \
    {                                                      \
        size_t i;                                          \
                                                           \
        compiler_barrier();                                \
        for (i = 0; i < size; i++)                         \
            arch_mmio_write##width##_relaxed(ptr, buf[i]); \
        compiler_barrier();                                \
    }                                                      \
    static inline void arch_mmio_read##width##_many(       \
        void *ptr, u##width *buf, size_t size              \
    )                                                      \
    {                                                      \
        size_t i;                                          \
                                                           \
        compiler_barrier();                                \
        for (i = 0; i < size; i++)                         \
            buf[i] = arch_mmio_read##width##_relaxed(ptr); \
        compiler_barrier();                                \
    }

X86_MAKE_MMIO_RW_MANY_STRICT(8)
X86_MAKE_MMIO_RW_MANY_STRICT(16)
X86_MAKE_MMIO_RW_MANY_STRICT(32)
X86_MAKE_MMIO_RW_MANY_STRICT(64)
