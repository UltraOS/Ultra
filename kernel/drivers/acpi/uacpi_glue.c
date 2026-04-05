#include <common/format.h>

#include <boot/boot.h>
#include <io.h>

#include <uacpi/kernel_api.h>

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *out_rsdp_address)
{
    *out_rsdp_address = g_boot_ctx.platform_info->acpi_rsdp_address;
    if (*out_rsdp_address == 0)
        return UACPI_STATUS_NOT_FOUND;

    return UACPI_STATUS_OK;
}

void *uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len)
{
    UNREFERENCED_PARAMETER(len);
    return phys_to_virt(addr);
}

void uacpi_kernel_unmap(void *addr, uacpi_size len)
{
    UNREFERENCED_PARAMETER(addr);
    UNREFERENCED_PARAMETER(len);
}

static u8 uacpi_log_level_to_syslog(uacpi_log_level lvl)
{
    switch (lvl) {
    case UACPI_LOG_DEBUG:
        return LOG_LEVEL_DEBUG;
    case UACPI_LOG_INFO:
    default:
        return LOG_LEVEL_INFO;
    case UACPI_LOG_WARN:
        return LOG_LEVEL_WARN;
    case UACPI_LOG_ERROR:
        return LOG_LEVEL_ERR;
    }
}

void uacpi_kernel_log(uacpi_log_level lvl, const uacpi_char *fmt, ...)
{
    va_list vlist;
    struct nested_printf npf;

    va_start(vlist, fmt);
    npf.fmt = fmt;
    npf.vlist = &vlist;

    print(
        "%c%cacpi: %pV", LOG_LEVEL_PREFIX_CHAR, uacpi_log_level_to_syslog(lvl),
        &npf
    );

    va_end(vlist);
}
