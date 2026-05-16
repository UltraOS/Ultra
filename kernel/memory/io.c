#define MSG_FMT(msg) "io: " msg

#include <common/helpers.h>
#include <common/error.h>
#include <common/bit.h>
#include <common/align.h>

#include <memory/alloc.h>
#include <memory/io.h>
#include <memory/page_table.h>
#include <memory/address_space.h>
#include <memory/tlb.h>
#include <boot/alloc.h>

#include <bug.h>
#include <free_after_init.h>
#include <init_level.h>
#include <log.h>

#include <private/memory.h>
#include <private/arch/io.h>
#include <arch/private/io.h>

/*
 * Aligns the physical address down to the nearest PT1_SIZE and expands the
 * size to encompass the newly aligned boundaries. Returns the offset that
 * must be added to the resulting virtual address.
 */
static inline size_t io_mapping_align(
    phys_addr_t *phys_in_out, size_t *size_in_out
)
{
    size_t offset;

    offset = *phys_in_out & (PT1_SIZE - 1);
    *phys_in_out = ALIGN_DOWN(*phys_in_out, PT1_SIZE);
    *size_in_out = ALIGN_UP(*size_in_out + offset, PT1_SIZE);

    return offset;
}

/*
 * Explanation of the macros below:
 * NUM_EARLY_PT2_RESERVED_TOTAL -> total number of pt2 tables reserved for
 *                                 early MMIO
 * NUM_EARLY_SMALL_PT2_RESERVED -> number of pt2 tables reserved for small
 *                                 early mappings
 * NUM_EARLY_LARGE_PT2_RESERVED -> number of pt2 tables reserved for large
 *                                 early mappings
 * PT1_PER_LARGE_EARLY_SLOT_SHIFT -> 2^N of pt1 slots to reserve for a single
 *                                   large mapping
 * NUM_BYTES_PER_LARGE_EARLY_SLOT -> maximum number of bytes mappable by a
 *                                   single large early io_window_map request
 *
 * EARLY_IO_MAP_BASE -> virtual address of the start of the early MMIO region
 * EARLY_IO_MAP_BASE_SMALL -> virtual address of the start of the early MMIO
 *                            region for small mappings
 * EARLY_IO_MAP_BASE_LARGE -> virtual address of the start of the early MMIO
 *                            region for large mappings
 */
#define NUM_EARLY_PT2_RESERVED_TOTAL 3

#define NUM_EARLY_SMALL_PT2_RESERVED 1
#define NUM_EARLY_LARGE_PT2_RESERVED \
    (NUM_EARLY_PT2_RESERVED_TOTAL - NUM_EARLY_SMALL_PT2_RESERVED)

#define NUM_EARLY_SMALL_PT1 ((PT2_SIZE * NUM_EARLY_SMALL_PT2_RESERVED) >> PT1_SHIFT)
#define NUM_EARLY_LARGE_PT1 ((PT2_SIZE * NUM_EARLY_LARGE_PT2_RESERVED) >> PT1_SHIFT)

#define PT1_PER_LARGE_EARLY_SLOT_SHIFT 6
#define NUM_BYTES_PER_LARGE_EARLY_SLOT \
    (1 << (PT1_SHIFT + PT1_PER_LARGE_EARLY_SLOT_SHIFT))

#define NUM_EARLY_SMALL_SLOTS NUM_EARLY_SMALL_PT1
#define NUM_EARLY_LARGE_SLOTS \
    (NUM_EARLY_LARGE_PT1 / (1 << PT1_PER_LARGE_EARLY_SLOT_SHIFT))

#define EARLY_IO_MAP_BASE (                                               \
    (UNSIGNED_MAX(ptr_t) - (PT2_SIZE * NUM_EARLY_PT2_RESERVED_TOTAL)) + 1 \
)
#define EARLY_IO_MAP_BASE_SMALL EARLY_IO_MAP_BASE
#define EARLY_IO_MAP_BASE_LARGE \
    (EARLY_IO_MAP_BASE_SMALL + (NUM_EARLY_SMALL_PT2_RESERVED * PT2_SIZE))

static struct pt3* INIT_DATA s_early_pt3;

static void* INIT_CODE io_pt_early_page_alloc(void)
{
    return boot_alloc_zeroed_or_die(1, "early MMIO remapping");
}

static pt_prot s_default_pt_prot;

void INIT_CODE early_io_map_init(void)
{
    struct pt5 *pt5;
    struct pt4 *pt4;
    struct pt3 *pt3;
    struct pt2 *pt2;
    size_t i;
    virt_addr_t va = EARLY_IO_MAP_BASE;

    s_default_pt_prot = pt_prot_from_vm_prot(
        VM_PROT_KERNEL | VM_PROT_READ | VM_PROT_WRITE
    );

    pt5 = pt_root_from_address_space(&g_kernel_address_space, va);
    if (pt5_none(pt5))
        pt5_populate(pt5, io_pt_early_page_alloc());

    pt4 = pt4_from_pt5(pt5, va);
    if (pt4_none(pt4))
        pt4_populate(pt4, io_pt_early_page_alloc());

    pt3 = pt3_from_pt4(pt4, va);
    if (pt3_none(pt3))
        pt3_populate(pt3, io_pt_early_page_alloc());

    s_early_pt3 = pt3;

    // Populate everything up to 0xFFFF...FFFF
    for (i = 0; i < NUM_EARLY_PT2_RESERVED_TOTAL; i++, va += PT2_SIZE) {
        pt2 = pt2_from_pt3(pt3, va);
        if (pt2_none(pt2))
            pt2_populate(pt2, io_pt_early_page_alloc());
    }

    pr_info(
        "early MMIO region at 0x%016zX, slots: %zu of %luK, %zu of %dK\n",
        EARLY_IO_MAP_BASE, NUM_EARLY_SMALL_SLOTS, PT1_SIZE / 1024,
        NUM_EARLY_LARGE_SLOTS, NUM_BYTES_PER_LARGE_EARLY_SLOT / 1024
    );
}

static void INIT_CODE do_io_window_early_map(
    virt_addr_t va, phys_addr_t pa, size_t length, pt_prot prot
)
{
    size_t i;
    struct pt2 *pt2;
    struct pt1 *pt1;

    for (i = 0; i < length; i += PT1_SIZE, va += PT1_SIZE, pa += PT1_SIZE) {
        pt2 = pt2_from_pt3(s_early_pt3, va);
        pt1 = pt1_from_pt2(pt2, va);
        pt1_populate(pt1, pa, prot);
    }
}

static MAKE_BITMAP(s_early_large_mappings, NUM_EARLY_LARGE_SLOTS) INIT_DATA;
static MAKE_BITMAP(s_early_small_mappings, NUM_EARLY_SMALL_SLOTS) INIT_DATA;

static virt_addr_t INIT_CODE alloc_small_mapping(void)
{
    reg_t slot;

    slot = find_first_zero_bit(s_early_small_mappings, NUM_EARLY_SMALL_SLOTS);
    if (unlikely(slot == NUM_EARLY_SMALL_SLOTS))
        return 0;

    bit_set(s_early_small_mappings, slot);
    return EARLY_IO_MAP_BASE_SMALL + (slot * PT1_SIZE);
}

static virt_addr_t INIT_CODE alloc_large_mapping(void)
{
    reg_t slot;

    slot = find_first_zero_bit(s_early_large_mappings, NUM_EARLY_LARGE_SLOTS);
    if (unlikely(slot == NUM_EARLY_LARGE_SLOTS))
        return 0;

    bit_set(s_early_large_mappings, slot);
    return EARLY_IO_MAP_BASE_LARGE + (slot * NUM_BYTES_PER_LARGE_EARLY_SLOT);
}

static void* INIT_CODE early_io_map_with_prot(
    phys_addr_t phys_base, size_t length, pt_prot prot
)
{
    virt_addr_t va = 0;
    size_t offset;

    offset = io_mapping_align(&phys_base, &length);

    if (length <= PT1_SIZE)
        va = alloc_small_mapping();

    if (va == 0 && length <= NUM_BYTES_PER_LARGE_EARLY_SLOT)
        va = alloc_large_mapping();

    if (va == 0)
        return nullptr;

    do_io_window_early_map(va, phys_base, length, prot);
    return (void*)(va + offset);
}

static void INIT_CODE do_io_window_early_unmap(
    virt_addr_t va_start, virt_addr_t va_end
)
{
    struct pt2 *pt2;
    struct pt1 *pt1;
    virt_addr_t va = va_start;

    while (va < va_end) {
        pt2 = pt2_from_pt3(s_early_pt3, va);
        pt1 = pt1_from_pt2(pt2, va);

        pt1_clear(pt1);
        va += PT1_SIZE;
    }

    tlb_invalidate_kernel_range(va_start, va_end);
}

static void INIT_CODE early_io_unmap(void *addr, size_t length)
{
    virt_addr_t va = (virt_addr_t)addr;
    virt_addr_t va_start;
    size_t offset;
    size_t real_length;
    reg_t slot;

    offset = va & (PT1_SIZE - 1);
    va_start = ALIGN_DOWN(va, PT1_SIZE);
    real_length = ALIGN_UP(length + offset, PT1_SIZE);

    if (va_start < EARLY_IO_MAP_BASE_LARGE) {
        BUG_ON(va_start < EARLY_IO_MAP_BASE_SMALL);
        BUG_ON(real_length > PT1_SIZE);

        slot = (va_start - EARLY_IO_MAP_BASE_SMALL) >> PT1_SHIFT;
        bit_clear(s_early_small_mappings, slot);
    } else {
        BUG_ON(real_length > NUM_BYTES_PER_LARGE_EARLY_SLOT);

        slot = (va_start - EARLY_IO_MAP_BASE_LARGE) >>
               (PT1_SHIFT + PT1_PER_LARGE_EARLY_SLOT_SHIFT);
        bit_clear(s_early_large_mappings, slot);
    }

    do_io_window_early_unmap(va_start, va_start + real_length);
}

static error_t CODE_REFERENCES_INIT_DATA io_window_map_with_prot(
    io_window *out_iow, phys_addr_t phys_base, size_t length,
    pt_prot prot
)
{
    void *mapping;

    // TODO: use this check to decide which map helper to use once we support it
    // if (init_level_below(INIT_LEVEL_IO_WINDOW_AVAILABLE))

    mapping = early_io_map_with_prot(phys_base, length, prot);
    if (mapping == NULL)
        return ENOMEM;

    out_iow->mmio_address = mapping;
    out_iow->type = IO_TYPE_MEM_IO;
    out_iow->length = length;

    return EOK;
}

error_t io_window_map(io_window *out_iow, phys_addr_t phys_base, size_t length)
{
    return io_window_map_with_prot(
        out_iow, phys_base, length, io_window_pt_prot(s_default_pt_prot)
    );
}

error_t io_window_map_wc(
    io_window *out_iow, phys_addr_t phys_base, size_t length
)
{
    pt_prot prot;

#ifdef ARCH_HAS_IO_WINDOW_WC_PT_PROT
    prot = io_window_wc_pt_prot(s_default_pt_prot);
#else
    prot = io_window_pt_prot(s_default_pt_prot);
#endif

    return io_window_map_with_prot(out_iow, phys_base, length, prot);
}

error_t io_window_map_wt(
    io_window *out_iow, phys_addr_t phys_base, size_t length
)
{
    pt_prot prot;

#ifdef ARCH_HAS_IO_WINDOW_WT_PT_PROT
    prot = io_window_wt_pt_prot(s_default_pt_prot);
#else
    prot = io_window_pt_prot(s_default_pt_prot);
#endif

    return io_window_map_with_prot(out_iow, phys_base, length, prot);
}

error_t io_window_map_np(
    io_window *out_iow, phys_addr_t phys_base, size_t length
)
{
    pt_prot prot;

#ifdef ARCH_HAS_IO_WINDOW_NP_PT_PROT
    prot = io_window_np_pt_prot(s_default_pt_prot);
#else
    prot = io_window_pt_prot(s_default_pt_prot);
#endif

    return io_window_map_with_prot(out_iow, phys_base, length, prot);
}

error_t io_window_map_pio(
    io_window *out_iow, phys_addr_t phys_base, size_t length
)
{
    pio_addr_or_error_t ret;

    ret = arch_map_pio(phys_base, length);
    if (error_pio_addr(ret))
        return decode_error_pio_addr(ret);

    out_iow->port_address = ret;
    out_iow->type = IO_TYPE_PORT_IO;
    out_iow->length = length;

    return EOK;
}

void* CODE_REFERENCES_INIT_DATA io_window_map_cached(
    phys_addr_t phys_base, size_t length
)
{
    return early_io_map_with_prot(phys_base, length, s_default_pt_prot);
}

void CODE_REFERENCES_INIT_DATA io_window_unmap_ptr(void *virt, size_t length)
{
    early_io_unmap(virt, length);
}

void io_window_unmap(io_window *iow, size_t size)
{
    switch (iow->type)
    {
    case IO_TYPE_MEM_IO:
        io_window_unmap_ptr(iow->mmio_address, size);
        break;
    case IO_TYPE_PORT_IO:
        arch_unmap_pio(iow->port_address, size);
        break;
        default:
    case IO_TYPE_UNMAPPED:
    case IO_TYPE_INVALID:
        BUG();
    }

    iow->type = IO_TYPE_UNMAPPED;
}

void *io_window_raw_ptr(io_window *iow)
{
    BUG_ON(iow->type != IO_TYPE_MEM_IO);
    return iow->mmio_address;
}

static void check_access(
    io_window *iow, size_t offset, size_t width, const char *type
)
{
    const char *why;

    if (unlikely(iow->type != IO_TYPE_MEM_IO &&
                 iow->type != IO_TYPE_PORT_IO)) {
        if (iow->type == IO_TYPE_UNMAPPED)
            why = "unmapped io window";
        else if (iow->type == IO_TYPE_INVALID)
            why = "uninitialized io window";
        else
            why = "corrupted io window";

        goto out_invalid;
    }

    if (unlikely(iow->type == IO_TYPE_PORT_IO && width > 4)) {
        why = "invalid access size";
        goto out_invalid;
    }

    if (unlikely((offset + width > iow->length) ||
                 (offset + width < offset))) {
        why = "out-of-bounds";
        goto out_invalid;
    }

    return;

out_invalid:
    panic(
        "Invalid %s IO window [%p len=%zu, type=%d] at %zu: %s",
        type, iow->mmio_address, iow->length, iow->type, offset, why
    );
}

#define MAKE_GENERIC_MMIO_MANY(width, fn_suffix)                             \
    static inline void arch_mmio_read##width##fn_suffix##_many(              \
        void *ptr, u##width *buf, size_t count                               \
    )                                                                        \
    {                                                                        \
        for (size_t i = 0; i < count; i++)                                   \
            buf[i] = arch_mmio_read##width##fn_suffix(ptr);                  \
    }                                                                        \
                                                                             \
    static inline void arch_mmio_write##width##fn_suffix##_many(             \
        void *ptr, const u##width *buf, size_t count                         \
    )                                                                        \
    {                                                                        \
        for (size_t i = 0; i < count; i++)                                   \
            arch_mmio_write##width##fn_suffix(ptr, buf[i]);                  \
    }

#ifndef ARCH_HAS_CUSTOM_MMIO_RW_MANY_STRICT
MAKE_GENERIC_MMIO_MANY(8, )
MAKE_GENERIC_MMIO_MANY(16, )
MAKE_GENERIC_MMIO_MANY(32, )
#if ULTRA_ARCH_WIDTH >= 8
MAKE_GENERIC_MMIO_MANY(64, )
#endif
#endif

#ifndef ARCH_HAS_CUSTOM_MMIO_RW_MANY_RELAXED
MAKE_GENERIC_MMIO_MANY(8, _relaxed)
MAKE_GENERIC_MMIO_MANY(16, _relaxed)
MAKE_GENERIC_MMIO_MANY(32, _relaxed)
#if ULTRA_ARCH_WIDTH >= 8
MAKE_GENERIC_MMIO_MANY(64, _relaxed)
#endif
#endif

#ifdef ARCH_HAS_CUSTOM_PIO
#define DO_MAKE_IO_FN(width, suffix, mmio_suffix)                              \
    u##width ioread##width##suffix(io_window *iow, size_t offset)              \
    {                                                                          \
        check_access(iow, offset, width / BITS_PER_BYTE, "read from");         \
                                                                               \
        if (iow->type == IO_TYPE_MEM_IO) {                                     \
            return arch_mmio_read##width##mmio_suffix(                         \
                iow->mmio_address + offset                                     \
            );                                                                 \
        }                                                                      \
                                                                               \
        return arch_pio_read##width##suffix(iow->port_address + offset);       \
    }                                                                          \
                                                                               \
    void iowrite##width##suffix(io_window *iow, size_t offset, u##width value) \
    {                                                                          \
        check_access(iow, offset, width / BITS_PER_BYTE, "write to");          \
                                                                               \
        if (iow->type == IO_TYPE_MEM_IO) {                                     \
            arch_mmio_write##width##mmio_suffix(                               \
                iow->mmio_address + offset, value                              \
            );                                                                 \
            return;                                                            \
        }                                                                      \
                                                                               \
        arch_pio_write##width##suffix(iow->port_address + offset, value);      \
    }

#define MAKE_IO_FN_MANY(width, suffix)                                         \
    void ioread##width##suffix##_many(                                         \
        io_window *iow, size_t offset, u##width *buf, size_t count             \
    )                                                                          \
    {                                                                          \
        check_access(iow, offset, width / BITS_PER_BYTE, "read from");         \
        if (iow->type == IO_TYPE_MEM_IO) {                                     \
            arch_mmio_read##width##suffix##_many(                              \
                iow->mmio_address + offset, buf, count                         \
            );                                                                 \
            return;                                                            \
        }                                                                      \
                                                                               \
        arch_pio_read##width##suffix##_many(                                   \
            iow->port_address + offset, buf, count                             \
        );                                                                     \
    }                                                                          \
                                                                               \
    void iowrite##width##suffix##_many(                                        \
        io_window *iow, size_t offset, const u##width *buf, size_t count       \
    )                                                                          \
    {                                                                          \
        check_access(iow, offset, width / BITS_PER_BYTE, "write to");          \
        if (iow->type == IO_TYPE_MEM_IO) {                                     \
            arch_mmio_write##width##suffix##_many(                             \
                iow->mmio_address + offset, buf, count                         \
            );                                                                 \
            return;                                                            \
        }                                                                      \
                                                                               \
        arch_pio_write##width##suffix##_many(                                  \
            iow->port_address + offset, buf, count                             \
        );                                                                     \
    }

#define MAKE_IO_FN_64(suffix)                                                  \
    u64 ioread64##suffix(io_window *iow, size_t offset)                        \
    {                                                                          \
        check_access(iow, offset, 64 / BITS_PER_BYTE, "read from");            \
        return arch_mmio_read64##suffix(iow->mmio_address + offset);           \
    }                                                                          \
                                                                               \
    void iowrite64##suffix(io_window *iow, size_t offset, u64 value)           \
    {                                                                          \
        check_access(iow, offset, 64 / BITS_PER_BYTE, "write to");             \
        arch_mmio_write64##suffix(iow->mmio_address + offset, value);          \
    }                                                                          \
                                                                               \
    void ioread64##suffix##_many(                                              \
        io_window *iow, size_t offset, u64 *buf, size_t count                  \
    )                                                                          \
    {                                                                          \
        check_access(iow, offset, 64 / BITS_PER_BYTE, "read from");            \
        arch_mmio_read64##suffix##_many(                                       \
            iow->mmio_address + offset, buf, count                             \
        );                                                                     \
    }                                                                          \
                                                                               \
    void iowrite64##suffix##_many(                                             \
        io_window *iow, size_t offset, const u64 *buf, size_t count            \
    )                                                                          \
    {                                                                          \
        check_access(iow, offset, 64 / BITS_PER_BYTE, "write to");             \
        arch_mmio_write64##suffix##_many(                                      \
            iow->mmio_address + offset, buf, count                             \
        );                                                                     \
    }

#define MAKE_IO_FN(width, suffix) DO_MAKE_IO_FN(width, suffix, suffix)
#define MAKE_IO_FN_SLOWDOWN(width) DO_MAKE_IO_FN(width, _slowdown, )
#else
#define MAKE_IO_FN(width, suffix)                                              \
    u##width ioread##width##suffix(io_window *iow, size_t offset)              \
    {                                                                          \
        check_access(iow, offset, width / BITS_PER_BYTE, "read from");         \
        return arch_mmio_read##width##suffix(iow->mmio_address + offset);      \
    }                                                                          \
                                                                               \
    void iowrite##width##suffix(io_window *iow, size_t offset, u##width value) \
    {                                                                          \
        check_access(iow, offset, width / BITS_PER_BYTE, "write to");          \
        arch_mmio_write##width##suffix(iow->mmio_address + offset, value);     \
    }                                                                          \

#define MAKE_IO_FN_MANY(width, suffix)                                         \
    void ioread##width##suffix##_many(                                         \
        io_window *iow, size_t offset, u##width *buf, size_t count             \
    )                                                                          \
    {                                                                          \
        check_access(iow, offset, width / BITS_PER_BYTE, "read from");         \
        arch_mmio_read##width##suffix##_many(                                  \
            iow->mmio_address + offset, buf, count                             \
        );                                                                     \
    }                                                                          \
                                                                               \
    void iowrite##width##suffix##_many(                                        \
        io_window *iow, size_t offset, const u##width *buf, size_t count       \
    )                                                                          \
    {                                                                          \
        check_access(iow, offset, width / BITS_PER_BYTE, "write to");          \
        arch_mmio_write##width##suffix##_many(                                 \
            iow->mmio_address + offset, buf, count                             \
        );                                                                     \
    }

#define MAKE_IO_FN_64(suffix)  \
    MAKE_IO_FN(64, suffix)     \
    MAKE_IO_FN_MANY(64, suffix)

#define MAKE_IO_FN_SLOWDOWN(width)                                   \
    u##width ioread##width##_slowdown(io_window *iow, size_t offset) \
    {                                                                \
        return ioread##width(iow, offset);                           \
    }                                                                \
                                                                     \
    void iowrite##width##_slowdown(                                  \
        io_window *iow, size_t offset, u##width value                \
    )                                                                \
    {                                                                \
       iowrite##width(iow, offset, value);                           \
    }

pio_addr_or_error_t arch_map_pio(phys_addr_t phys_base, size_t length)
{
    /*
     * TODO: implement generic PIO over MMIO for the non-ARCH_HAS_CUSTOM_PIO
     *       case
     */
    UNREFERENCED_PARAMETER(phys_base);
    UNREFERENCED_PARAMETER(length);

    return encode_error_pio_addr(ENOTSUP);
}

void arch_unmap_pio(pio_addr_t addr, size_t length)
{
    UNREFERENCED_PARAMETER(addr);
    UNREFERENCED_PARAMETER(length);
}
#endif

MAKE_IO_FN(8, )
MAKE_IO_FN(8, _relaxed)
MAKE_IO_FN_MANY(8, )
MAKE_IO_FN_MANY(8, _relaxed)
MAKE_IO_FN_SLOWDOWN(8)

MAKE_IO_FN(16, )
MAKE_IO_FN(16, _relaxed)
MAKE_IO_FN_MANY(16, )
MAKE_IO_FN_MANY(16, _relaxed)
MAKE_IO_FN_SLOWDOWN(16)

MAKE_IO_FN(32, )
MAKE_IO_FN(32, _relaxed)
MAKE_IO_FN_MANY(32, )
MAKE_IO_FN_MANY(32, _relaxed)
MAKE_IO_FN_SLOWDOWN(32)


#if ULTRA_ARCH_WIDTH == 8
MAKE_IO_FN_64( )
MAKE_IO_FN_64(_relaxed)
#endif
