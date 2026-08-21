#include <cpu_mask.h>

#if IS_ENABLED(CPU_MASK_OUT_OF_LINE)

#include <memory/alloc.h>
#include <bug.h>

error_t cpu_mask_handle_alloc(cpu_mask_handle *out_mask)
{
    // The size of a handle is not known until the CPUs are counted
    BUG_ON(g_num_present_cpus == 0);

    *out_mask = alloc(CPU_MASK_NUM_BYTES, ALLOC_GENERIC_ZEROED);
    if (*out_mask == NULL)
        return ENOMEM;

    return EOK;
}

void cpu_mask_handle_free(cpu_mask_handle mask)
{
    free(mask);
}

#endif
