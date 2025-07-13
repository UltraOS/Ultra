#include <common/string.h>

#include <leb128.h>
#include <symbols.h>
#include <private/symbols.h>

static u16 get_compressed_symbol_length(const u8 **cursor)
{
    u16 len;

    len = *(*cursor)++;
    if (len & LEB128_HAS_NEXT_BYTE) {
        len &= LEB128_MAX_PER_BYTE;
        len |= *(*cursor)++ << LEB128_BITS_PER_BYTE;
    }

    return len;
}

static void uncompress_symbol(
    const u8 *compressed_cursor, char out_buf[MAX_SYMBOL_LENGTH]
)
{
    u16 len;
    const char *token;
    char *out_cursor = out_buf;

    len = get_compressed_symbol_length(&compressed_cursor);

    while (len--) {
        token = &g_symbol_token_table[
            g_symbol_token_offsets[*compressed_cursor++]
        ];

        while (*token)
            *out_cursor++ = *token++;
    }

    *out_cursor = '\0';
}

static const u8 *get_compressed_symbol_name_cursor(u32 index)
{
    const u8 *cursor;
    u32 i;

    cursor = &g_symbol_compressed_names[g_symbol_name_offsets[index >> 8]];
    index &= 0xFF;

    for (i = 0; i < index; i++)
        cursor += get_compressed_symbol_length(&cursor);

    return cursor;
}

error_t symbol_lookup_by_address(
    ptr_t address, char out_name_buf[MAX_SYMBOL_LENGTH],
    size_t *out_offset_within
)
{
    u32 rel_address, symbol_base;
    u32 i, begin = 0, end = g_symbol_count;

    if (unlikely(!address_is_kernel_code(address)))
        return EINVAL;
    if (unlikely(g_symbol_count == 0))
        return ENOSYS;

    rel_address = address - g_symbol_base;

    while (end - begin > 1) {
        i = begin + ((end - begin) / 2);

        if (g_symbol_relative_addresses[i] <= rel_address)
            begin = i;
        else
            end = i;
    }

    symbol_base = g_symbol_relative_addresses[begin];
    if (out_offset_within != NULL)
        *out_offset_within = rel_address - symbol_base;

    uncompress_symbol(
        get_compressed_symbol_name_cursor(begin),
        out_name_buf
    );
    return EOK;
}
