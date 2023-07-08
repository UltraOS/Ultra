#include <hypervisor.h>
#include <cpuid.h>

static int is_in_hyperv = -1;

#define HYPERVISOR_BIT (1 << 31)

bool in_hypervisor(void)
{
    if (is_in_hyperv == -1) {
        struct cpuid_res res;
        cpuid(1, &res);

        is_in_hyperv = (res.c & HYPERVISOR_BIT) != 0;
    }

    return is_in_hyperv;
}
