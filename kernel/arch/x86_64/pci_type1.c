#include <common/bit.h>

#include <memory/io.h>
#include <init_level.h>
#include <free_after_init.h>
#include <spinlock.h>

#include <arch/private/pci_type1.h>

// The address port followed by the data port
#define TYPE1_ADDRESS_PORT 0xCF8
#define TYPE1_WINDOW_LEN 8

#define TYPE1_ADDRESS 0
    #define TYPE1_ADDRESS_REGISTER_MASK MAKE_BIT_MASK_U32(7, 2)
    #define TYPE1_ADDRESS_FUNCTION_MASK MAKE_BIT_MASK_U32(10, 8)
    #define TYPE1_ADDRESS_DEVICE_MASK MAKE_BIT_MASK_U32(15, 11)
    #define TYPE1_ADDRESS_BUS_MASK MAKE_BIT_MASK_U32(23, 16)
    #define TYPE1_ADDRESS_ENABLE BIT_U32(31)
#define TYPE1_DATA 4

static io_window s_type1_iow;
static DEFINE_SPINLOCK(s_type1_lock);

static error_t type1_check(struct pci_address addr, u16 reg, u8 width)
{
    if (addr.segment != 0 || reg >= PCI_CONFIG_SPACE_SIZE)
        return ENXIO;

    if (addr.device >= PCI_DEVICES_PER_BUS)
        return EINVAL;
    if (addr.function >= PCI_FUNCTIONS_PER_DEVICE)
        return EINVAL;
    if (width != sizeof(u8) && width != sizeof(u16) && width != sizeof(u32))
        return EINVAL;
    if (reg % width)
        return EINVAL;

    return EOK;
}

static u32 type1_address(struct pci_address addr, u16 reg)
{
    u32 value = TYPE1_ADDRESS_ENABLE;

    value |= BIT_FIELD_MAKE(TYPE1_ADDRESS_BUS_MASK, addr.bus);
    value |= BIT_FIELD_MAKE(TYPE1_ADDRESS_DEVICE_MASK, addr.device);
    value |= BIT_FIELD_MAKE(TYPE1_ADDRESS_FUNCTION_MASK, addr.function);
    value |= reg & TYPE1_ADDRESS_REGISTER_MASK;

    return value;
}

/*
 * Selects the dword holding the register and returns the data port offset
 * of the register within it, the lock is held until type1_deselect()
 */
static error_t type1_select(
    struct pci_address addr, u16 reg, u8 width, irq_state_t *out_state,
    size_t *out_offset
)
{
    error_t ret;

    ret = type1_check(addr, reg, width);
    if (is_error(ret))
        return ret;

    *out_offset = TYPE1_DATA + (reg % sizeof(u32));
    *out_state = spin_lock_irq_save(&s_type1_lock);
    iowrite32(&s_type1_iow, TYPE1_ADDRESS, type1_address(addr, reg));

    return EOK;
}

static void type1_deselect(irq_state_t state)
{
    spin_unlock_irq_restore(&s_type1_lock, state);
}

error_t pci_type1_read(
    struct pci_address addr, u16 reg, u8 width, u32 *out_value
)
{
    error_t ret;
    irq_state_t state;
    size_t offset;

    ret = type1_select(addr, reg, width, &state, &offset);
    if (is_error(ret))
        return ret;

    switch (width) {
    case sizeof(u8):
        *out_value = ioread8(&s_type1_iow, offset);
        break;
    case sizeof(u16):
        *out_value = ioread16(&s_type1_iow, offset);
        break;
    default:
        *out_value = ioread32(&s_type1_iow, offset);
        break;
    }

    type1_deselect(state);
    return EOK;
}

error_t pci_type1_write(struct pci_address addr, u16 reg, u8 width, u32 value)
{
    error_t ret;
    irq_state_t state;
    size_t offset;

    ret = type1_select(addr, reg, width, &state, &offset);
    if (is_error(ret))
        return ret;

    switch (width) {
    case sizeof(u8):
        iowrite8(&s_type1_iow, offset, value);
        break;
    case sizeof(u16):
        iowrite16(&s_type1_iow, offset, value);
        break;
    default:
        iowrite32(&s_type1_iow, offset, value);
        break;
    }

    type1_deselect(state);
    return EOK;
}

static error_t INIT_CODE pci_type1_init(void)
{
    return io_window_map_pio(
        TYPE1_ADDRESS_PORT, TYPE1_WINDOW_LEN, &s_type1_iow
    );
}
INIT_CALL_POST(PRE_BOOT, pci_type1_init);
