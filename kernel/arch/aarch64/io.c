#include <common/types.h>
#include <common/error.h>
#include <private/arch/io.h>

ptr_or_error_t arch_map_memory_io(phys_addr_t phys_base, size_t length)
{
    UNREFERENCED_PARAMETER(phys_base);
    UNREFERENCED_PARAMETER(length);
    return encode_error_ptr(ENOTSUP);
}

pio_addr_t arch_map_port_io(phys_addr_t phys_base, size_t length)
{
    UNREFERENCED_PARAMETER(phys_base);
    UNREFERENCED_PARAMETER(length);
    return encode_error_pio_addr(ENOTSUP);
}
