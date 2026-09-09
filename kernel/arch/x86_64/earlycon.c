#define MSG_FMT(msg) "earlycon: " msg

#include <common/bit.h>
#include <common/conversions.h>
#include <common/error.h>
#include <common/helpers.h>
#include <common/types.h>

#include <module.h>
#include <free_after_init.h>
#include <console.h>
#include <param.h>
#include <arch/cpu_helpers.h>
#include <arch/private/cpu.h>
#include <arch/private/early_pci.h>

#include <pci/address.h>

#include <memory/io.h>

static io_window s_earlycon_iow;

static void e9_write(struct console *con, const char *str, size_t count)
{
    UNREFERENCED_PARAMETER(con);
    iowrite8_relaxed_many(&s_earlycon_iow, 0, (const u8*)str, count);
}

static struct console e9_console = {
    .name = "E9 debugcon",
    .write = e9_write,
};

enum ns16550_reg {
    NS16550_REG_THR = 0,
    NS16550_REG_IER = 1,
    NS16550_REG_FCR = 2,
    NS16550_REG_LCR = 3,
        #define NS16550_LCR_WORD_LENGTH_MASK MAKE_BIT_MASK_U8(1, 0)
        #define NS16550_LCR_DLAB BIT_U8(7)

    NS16550_REG_MCR = 4,
        #define NS16550_MCR_DTR BIT_U8(0)
        #define NS16550_MCR_RTS BIT_U8(1)

    NS16550_REG_LSR = 5,
        #define NS16550_LSR_THRE BIT_U8(5)
        #define NS16550_LSR_TEMT BIT_U8(6)

    // Accessible if NS16550_LCR_DLAB is enabled in NS16550_REG_LCR
    NS16550_REG_DLL = 0,
    NS16550_REG_DLM = 1,
};
#define NS16550_NUM_REGS 8

// The baud is the clock divided by 16 times the divisor
#define NS16550_CLOCK_HZ 1843200
#define NS16550_CLOCK_DIVIDER 16
#define NS16550_MAX_DIVISOR UNSIGNED_MAX(u16)
#define NS16550_DEFAULT_BAUD 115200

static u8 ns16550_read(enum ns16550_reg reg)
{
    return ioread8(&s_earlycon_iow, reg);
}

static void ns16550_write(enum ns16550_reg reg, u8 value)
{
    iowrite8(&s_earlycon_iow, reg, value);
}

static void ns16550_wait_lsr(u8 bits)
{
    while ((ns16550_read(NS16550_REG_LSR) & bits) != bits)
        arch_cpu_relax();
}

static void ns16550_putc(char c)
{
    ns16550_wait_lsr(NS16550_LSR_THRE);
    ns16550_write(NS16550_REG_THR, c);
}

static void ns16550_console_write(
    struct console *con, const char *str, size_t count
)
{
    UNREFERENCED_PARAMETER(con);

    while (count--) {
        if (*str == '\n')
            ns16550_putc('\r');

        ns16550_putc(*str++);
    }

    /*
     * Wait for the write to fully complete before returning (both THR and TSR
     * must be empty).
     */
    ns16550_wait_lsr(NS16550_LSR_TEMT);
}

static struct console ns16550_console = {
    .name = "ns16550",
    .write = ns16550_console_write,
};

// The console currently registered, it owns s_earlycon_iow
static struct console *INIT_DATA s_active_console;

static error_t INIT_CODE earlycon_activate(struct console *con, bool colored)
{
    error_t ret;

    con->flags = colored ? CONSOLE_FLAG_ANSI_COLOR : CONSOLE_FLAG_NONE;

    ret = register_console(con);
    if (is_error(ret))
        return ret;

    s_active_console = con;
    return EOK;
}

static error_t INIT_CODE earlycon_destroy(void)
{
    error_t ret;

    if (s_active_console == nullptr)
        return EOK;

    ret = unregister_console(s_active_console);
    if (is_error(ret))
        return ret;

    io_window_unmap(&s_earlycon_iow);
    s_active_console = nullptr;
    return EOK;
}

static error_t INIT_CODE e9_console_init(bool colored)
{
    error_t ret;

    if (!all_cpus_have(X86_FEATURE_HYPERVISOR))
        return ENODEV;

    ret = io_window_map_pio(0xE9, 1, &s_earlycon_iow);
    if (is_error(ret))
        return ret;

    ret = ENODEV;
    if (ioread8(&s_earlycon_iow, 0) != 0xE9)
        goto unmap;

    ret = earlycon_activate(&e9_console, colored);
    if (is_error(ret))
        goto unmap;

    return EOK;

unmap:
    io_window_unmap(&s_earlycon_iow);
    return ret;
}

enum ns16550_source_kind {
    NS16550_SOURCE_NONE,
    NS16550_SOURCE_IO,
    NS16550_SOURCE_PCI,
};

struct ns16550_source {
    enum ns16550_source_kind kind;
    u16 io_base;
    struct pci_address pci;
};

static error_t INIT_CODE ns16550_source_claim(
    struct ns16550_source *src, enum ns16550_source_kind kind
)
{
    if (src->kind != NS16550_SOURCE_NONE)
        return EEXIST;

    src->kind = kind;
    return EOK;
}

static error_t INIT_CODE ns16550_io_source_set(
    struct string value, struct param_value *v, bool is_runtime
)
{
    error_t ret;
    u16 io_base;
    struct ns16550_source *src = v->ptr;

    UNREFERENCED_PARAMETER(is_runtime);

    ret = str_to_u16(value, &io_base);
    if (is_error(ret))
        return ret;

    ret = ns16550_source_claim(src, NS16550_SOURCE_IO);
    if (is_error(ret))
        return ret;

    src->io_base = io_base;
    return EOK;
}

static error_t INIT_CODE ns16550_pci_source_set(
    struct string value, struct param_value *v, bool is_runtime
)
{
    error_t ret;
    struct pci_address pci;
    struct ns16550_source *src = v->ptr;

    UNREFERENCED_PARAMETER(is_runtime);

    ret = str_to_pci_address(value, &pci);
    if (is_error(ret))
        return ret;

    ret = ns16550_source_claim(src, NS16550_SOURCE_PCI);
    if (is_error(ret))
        return ret;

    src->pci = pci;
    return EOK;
}

static bool INIT_CODE pci_class_is_16550(u32 class_code)
{
    u32 class, interface;

    class = BIT_FIELD_READ(class_code, PCI_CLASS_CODE_CLASS_MASK);
    interface = BIT_FIELD_READ(class_code, PCI_CLASS_CODE_INTERFACE_MASK);

    if (class != PCI_CLASS_SERIAL_CONTROLLER && class != PCI_CLASS_MODEM)
        return false;

    return interface == PCI_SERIAL_INTERFACE_16550;
}

/*
 * The function must be a 16550 compatible with its registers behind an I/O
 * BAR0 that firmware has assigned, only the decode may be left disabled.
 */
static error_t INIT_CODE ns16550_pci_source_resolve(struct ns16550_source *src)
{
    error_t ret;
    struct pci_address addr = src->pci;
    u32 class_code, bar, base;
    u16 vendor, command;

    ret = early_pci_read16(addr, PCI_CONFIG_VENDOR_ID, &vendor);
    if (is_error(ret))
        return ret;

    if (vendor == PCI_VENDOR_ID_NONE) {
        pr_err("no function at %pPCI\n", &addr);
        return ENODEV;
    }

    ret = early_pci_read32(addr, PCI_CONFIG_CLASS_CODE, &class_code);
    if (is_error(ret))
        return ret;

    if (!pci_class_is_16550(class_code)) {
        pr_err(
            "%pPCI is not a 16550 compatible serial controller (class %06X)\n",
            &addr, BIT_FIELD_READ(class_code, PCI_CLASS_CODE_MASK)
        );
        return ENODEV;
    }

    ret = early_pci_read32(addr, PCI_CONFIG_BAR0, &bar);
    if (is_error(ret))
        return ret;

    if (!(bar & PCI_BAR_IO_SPACE)) {
        pr_err(
            "BAR0 of %pPCI is memory mapped, only I/O is supported\n", &addr
        );
        return ENOTSUP;
    }

    base = bar & PCI_BAR_IO_ADDRESS_MASK;
    if (base == 0) {
        pr_err("firmware assigned no I/O range to %pPCI\n", &addr);
        return ENODEV;
    }

    ret = early_pci_read16(addr, PCI_CONFIG_COMMAND, &command);
    if (is_error(ret))
        return ret;

    if (!(command & PCI_COMMAND_IO_ENABLE)) {
        command |= PCI_COMMAND_IO_ENABLE;
        ret = early_pci_write16(addr, PCI_CONFIG_COMMAND, command);
        if (is_error(ret))
            return ret;
    }

    src->io_base = base;
    return EOK;
}

static error_t INIT_CODE ns16550_baud_to_divisor(u32 baud, u16 *out_divisor)
{
    u64 divisor;

    if (baud == 0)
        return EINVAL;

    divisor = CLOSEST_DIVIDE(
        NS16550_CLOCK_HZ, (u64)NS16550_CLOCK_DIVIDER * baud
    );
    if (divisor == 0 || divisor > NS16550_MAX_DIVISOR)
        return EINVAL;

    *out_divisor = divisor;
    return EOK;
}

static void INIT_CODE ns16550_set_divisor(u16 divisor)
{
    ns16550_write(NS16550_REG_LCR, NS16550_LCR_DLAB);
    ns16550_write(NS16550_REG_DLL, divisor);
    ns16550_write(NS16550_REG_DLM, divisor >> BITS_PER_TYPE(u8));
}

static void INIT_CODE ns16550_setup(u16 divisor)
{
    // No interrupts needed for our earlycon (not that we can serve them anyway)
    ns16550_write(NS16550_REG_IER, 0);

    // FIFO disabled because we don't even know if it works on this 16550
    ns16550_write(NS16550_REG_FCR, 0);

    ns16550_set_divisor(divisor);

    // 8-bit words (both WLS0 and WLS1 set to 1)
    ns16550_write(
        NS16550_REG_LCR, BIT_FIELD_MAKE(NS16550_LCR_WORD_LENGTH_MASK, 0b11)
    );

    // Data Terminal Ready & Request to Send
    ns16550_write(NS16550_REG_MCR, NS16550_MCR_DTR | NS16550_MCR_RTS);
}

static error_t INIT_CODE ns16550_console_init(
    struct ns16550_source *src, u32 baud, bool colored
)
{
    error_t ret;
    u16 divisor;

    ret = ns16550_baud_to_divisor(baud, &divisor);
    if (is_error(ret)) {
        pr_err("unsupported baud %u\n", baud);
        return ret;
    }

    switch (src->kind) {
    case NS16550_SOURCE_PCI:
        ret = ns16550_pci_source_resolve(src);
        if (is_error(ret))
            return ret;
        FALLTHROUGH;
    case NS16550_SOURCE_IO:
        ret = io_window_map_pio(
            src->io_base, NS16550_NUM_REGS, &s_earlycon_iow
        );
        break;
    default:
        pr_err("ns16550 needs an io= port or a pci= function\n");
        return EINVAL;
    }
    if (is_error(ret))
        return ret;

    ns16550_setup(divisor);

    ret = earlycon_activate(&ns16550_console, colored);
    if (is_error(ret)) {
        io_window_unmap(&s_earlycon_iow);
        return ret;
    }

    if (src->kind == NS16550_SOURCE_PCI) {
        pr_info(
            "ns16550 at %pPCI, I/O port 0x%04X, %u baud\n", &src->pci,
            src->io_base, baud
        );
    } else {
        pr_info("ns16550 at I/O port 0x%04X, %u baud\n", src->io_base, baud);
    }

    return EOK;
}

enum earlycon_mode {
    EARLYCON_MODE_NONE,
    EARLYCON_MODE_E9,
    EARLYCON_MODE_NS16550,
};

static error_t INIT_CODE earlycon_set(
    struct string value, struct param_value *v, bool is_runtime
)
{
    error_t ret;
    size_t mode;
    bool colored = false;
    u32 baud = NS16550_DEFAULT_BAUD;
    struct ns16550_source source = { 0 };
    struct suboption options[] = { suboption(colored) };
    struct suboption ns16550_options[] = {
        custom_suboption(
            io, source, ns16550_io_source_set, SUBOPTION_FLAG_NONE
        ),
        custom_suboption(
            pci, source, ns16550_pci_source_set, SUBOPTION_FLAG_NONE
        ),
        suboption(baud),
        suboption(colored),
    };
    struct suboption_variant modes[] = {
        [EARLYCON_MODE_NONE] = SUBOPTION_VARIANT("none"),
        [EARLYCON_MODE_E9] = SUBOPTION_VARIANT("e9", options),
        [EARLYCON_MODE_NS16550] = SUBOPTION_VARIANT("ns16550", ns16550_options),
    };

    UNREFERENCED_PARAMETER(v);

    ret = parse_suboptions_by_head(
        value, modes, ARRAY_SIZE(modes), &mode, is_runtime
    );
    if (is_error(ret))
        return ret;

    ret = earlycon_destroy();
    if (is_error(ret))
        return ret;

    switch (mode) {
    case EARLYCON_MODE_E9:
        ret = e9_console_init(colored);
        break;
    case EARLYCON_MODE_NS16550:
        ret = ns16550_console_init(&source, baud, colored);
        break;
    default:
        return EOK;
    }
    if (is_error(ret))
        return ret;

    pr_info(
        "using%s '%pS' as the early console\n",
        colored ? " (colored)" : "", &modes[mode].head
    );
    return EOK;
}
init_action_parameter(earlycon, earlycon_set);
