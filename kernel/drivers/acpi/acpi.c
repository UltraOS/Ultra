#include <common/types.h>
#include <common/attributes.h>

#include <arch/constants.h>

#include <free_after_init.h>
#include <init_level.h>

#include <uacpi/uacpi.h>

INIT_DATA ALIGN(ULTRA_ARCH_WIDTH) u8 early_table_buf[PAGE_SIZE];

static error_t INIT_CODE acpi_setup_tables(void)
{
    uacpi_setup_early_table_access(early_table_buf, sizeof(early_table_buf));
    return EOK;
}
INIT_CALL_AT(PLATFORM_INFO_AVAILABLE, acpi_setup_tables);
