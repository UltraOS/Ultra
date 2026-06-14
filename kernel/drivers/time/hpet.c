#define MSG_FMT(msg) "hpet: " msg

#include <time/counter_device.h>

#include <uacpi/tables.h>
#include <uacpi/acpi.h>

#include <memory/io.h>
#include <init_level.h>
#include <log.h>

#include <common/bit.h>

enum hpet_reg {
    HPET_REG_CAPID = 0x00,
        #define REV_ID MAKE_BIT_MASK(7, 0)
        #define NUM_TIM_CAP MAKE_BIT_MASK(12, 8)
        #define COUNT_SIZE_CAP BIT(13)
        #define LEG_RT_CAP BIT(15)
        #define VENDOR_ID MAKE_BIT_MASK(31, 16)
        #define COUNTER_CLOCK_PERIOD MAKE_BIT_MASK(63, 32)

    HPET_REG_CONFIG = 0x10,
        #define ENABLE_CNF BIT(0)
        #define LEG_RT_CNF BIT(1)

    HPET_REG_INT_STATUS = 0x20,
    HPET_REG_MAIN_COUNTER = 0xF0,
};

static io_window s_hpet_io;

static u64 hpet_read(enum hpet_reg reg)
{
    return ioread64(&s_hpet_io, reg);
}

static void hpet_write(enum hpet_reg reg, u64 value)
{
    iowrite64(&s_hpet_io, reg, value);
}

static u64 hpet_read_cd(struct counter_device *cd)
{
    UNREFERENCED_PARAMETER(cd);
    return hpet_read(HPET_REG_MAIN_COUNTER);
}

static struct counter_device s_hpet_cd = {
    .name = "hpet",
    .read = hpet_read_cd,
    .mask = COUNTER_MASK(32),
    .rating = COUNTER_DEVICE_RATING_GREAT,
};

// 10 MHz minimum (per spec)
#define HPET_MAX_PERIOD 100000000

// 10 GHz maximum (otherwise the counter wraps too quickly)
#define HPET_MIN_PERIOD 100000

static error_t hpet_init(void)
{
    uacpi_status uret;
    error_t ret;
    uacpi_table tbl;
    struct acpi_hpet *hpet;
    phys_addr_t address;
    u64 capid, period, config;
    u8 bitness = 32;

    uret = uacpi_table_find_by_signature(ACPI_HPET_SIGNATURE, &tbl);
    if (uret == UACPI_STATUS_NOT_FOUND)
        return EOK;

    if (uacpi_unlikely_error(uret))
        return ENXIO;

    hpet = tbl.ptr;
    if (hpet->address.address_space_id != ACPI_AS_ID_SYS_MEM) {
        pr_warn(
            "timer not in SystemMemory: %d\n",
            hpet->address.address_space_id
        );
        return ENOSYS;
    }
    address = hpet->address.address;

    // Timer 31 ends at 0x3FF
    ret = io_window_map(&s_hpet_io, address, 0x3FF + 1);
    if (is_error(ret)) {
        pr_warn("unable to map: %d\n", ret);
        return ret;
    }

    capid = hpet_read(HPET_REG_CAPID);
    if (capid & COUNT_SIZE_CAP) {
        s_hpet_cd.mask = COUNTER_MASK(64);
        bitness = 64;
    }

    period = BIT_FIELD_READ(capid, COUNTER_CLOCK_PERIOD);
    if (unlikely(period < HPET_MIN_PERIOD || period > HPET_MAX_PERIOD)) {
        pr_warn("bogus period value 0x%08llX, ignoring\n", period);
        io_window_unmap(&s_hpet_io);
        return ENXIO;
    }

    pr_info("at 0x%llX, %d-bit counter\n", address, bitness);

    config = hpet_read(HPET_REG_CONFIG);
    config |= ENABLE_CNF;
    hpet_write(HPET_REG_CONFIG, config);

    counter_device_register(&s_hpet_cd, FS_PER_SEC / period);
    return ret;
}
INIT_CALL_POST(PLATFORM_INFO_AVAILABLE, hpet_init);
