#include <common/types.h>
#include <common/error.h>
#include <common/minmax.h>

#include <linker.h>

DECLARE_LINKER_SYMBOL(text_begin);
DECLARE_LINKER_SYMBOL(text_end);

DECLARE_LINKER_SYMBOL(eh_frame_hdr_begin);
DECLARE_LINKER_SYMBOL(eh_frame_hdr_end);

DECLARE_LINKER_SYMBOL(eh_frame_begin);
DECLARE_LINKER_SYMBOL(eh_frame_end);

// 127 chars & a NULL terminator
#define MAX_SYMBOL_LENGTH (127 + 1)

static inline bool address_is_kernel_code(ptr_t address)
{
    return within_bounds_inclusive(
        address, LINKER_SYMBOL_ADDR(text_begin),
                 LINKER_SYMBOL_ADDR(text_end)
    );
}

error_t symbol_lookup_by_address(
    ptr_t address, char out_name_buf[MAX_SYMBOL_LENGTH],
    size_t *out_offset_within
);
