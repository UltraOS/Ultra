#include <common/types.h>
#include <common/error.h>

extern u8 g_linker_symbol_text_begin[];
extern u8 g_linker_symbol_text_end[];

extern u8 g_linker_symbol_eh_frame_hdr_begin[];
extern u8 g_linker_symbol_eh_frame_hdr_end[];

extern u8 g_linker_symbol_eh_frame_begin[];
extern u8 g_linker_symbol_eh_frame_end[];

// 127 chars & a NULL terminator
#define MAX_SYMBOL_LENGTH (127 + 1)

static inline bool address_is_kernel_code(ptr_t address)
{
    return address >= (ptr_t)g_linker_symbol_text_begin &&
           address <= (ptr_t)g_linker_symbol_text_end;
}

error_t symbol_lookup_by_address(
    ptr_t address, char out_name_buf[MAX_SYMBOL_LENGTH],
    size_t *out_offset_within
);
