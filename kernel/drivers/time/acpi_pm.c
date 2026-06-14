#define MSG_FMT(msg) "acpi-pm: " msg

#include <time/counter_device.h>

#include <uacpi/tables.h>
#include <uacpi/acpi.h>

#include <memory/io.h>
#include <init_level.h>
#include <log.h>

#define PM_TIMER_HZ 3579545

static io_window s_pm_io;

static u64 pm_timer_read_cd(struct counter_device *cd)
{
    UNREFERENCED_PARAMETER(cd);
    return ioread32(&s_pm_io, 0);
}

static struct counter_device s_pm_timer_cd = {
    .name = "acpi-pm",
    .read = pm_timer_read_cd,
    .mask = COUNTER_MASK(24),
    .rating = COUNTER_DEVICE_RATING_GOOD,
};

static error_t pm_timer_init(void)
{
    uacpi_status uret;
    error_t ret;
    struct acpi_fadt *fadt;
    u16 port = 0;
    u8 bitness = 24;

    uret = uacpi_table_fadt(&fadt);
    if (uacpi_unlikely_error(uret))
        return ENXIO;

    if (fadt->flags & ACPI_TMR_VAL_EXT) {
        s_pm_timer_cd.mask = COUNTER_MASK(32);
        s_pm_timer_cd.rating++;
        bitness = 32;
    }

    if (fadt->hdr.revision >= 3) {
        if (fadt->x_pm_tmr_blk.address_space_id != ACPI_AS_ID_SYS_IO) {
            pr_warn(
                "timer not in SystemIO: %d\n",
                fadt->x_pm_tmr_blk.address_space_id
            );
            return ENOSYS;
        }

        port = fadt->x_pm_tmr_blk.address;
    }

    if (!port)
        port = fadt->pm_tmr_blk;
    if (!port)
        return EOK;

    pr_info("at 0x%04X, %d-bit counter\n", port, bitness);

    ret = io_window_map_pio(&s_pm_io, port, 4);
    if (is_error(ret)) {
        pr_warn("unable to map: %d\n", ret);
        return ret;
    }

    counter_device_register(&s_pm_timer_cd, PM_TIMER_HZ);
    return ret;
}
INIT_CALL_POST(PLATFORM_INFO_AVAILABLE, pm_timer_init);
