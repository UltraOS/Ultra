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

static struct string uncompress_symbol(
    const u8 *compressed_cursor, char out_buf[static MAX_SYMBOL_LENGTH]
)
{
    u16 len;
    const char *token;
    char *out_cursor = out_buf, *out_last;

    len = get_compressed_symbol_length(&compressed_cursor);
    out_last = &out_buf[MAX_SYMBOL_LENGTH - 1];

    while (len--) {
        token = &g_symbol_token_table[
            g_symbol_token_offsets[*compressed_cursor++]
        ];

        while (*token && out_cursor != out_last)
            *out_cursor++ = *token++;
    }

    *out_cursor = '\0';
    return MAKE_STR(out_buf, out_cursor - out_buf);
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

static struct string do_lookup_symbol(
    ptr_t address, char out_name_buf[static MAX_SYMBOL_LENGTH],
    size_t *out_offset_within, size_t *out_size, bool ensure_within_bounds
)
{
    u32 rel_address, offset, i, begin = 0, end = g_symbol_count;

    if (unlikely(g_symbol_count == 0))
        return NULL_STR();

    rel_address = address - LINKER_SYMBOL_ADDR(text_begin);

    while (end - begin > 1) {
        i = begin + ((end - begin) / 2);

        if (g_symbol_relative_addresses[i] <= rel_address)
            begin = i;
        else
            end = i;
    }

    offset = rel_address - g_symbol_relative_addresses[begin];
    if (ensure_within_bounds && offset >= g_symbol_sizes[begin])
        return NULL_STR();

    if (out_offset_within != nullptr)
        *out_offset_within = offset;
    if (out_size != nullptr)
        *out_size = g_symbol_sizes[begin];

    return uncompress_symbol(
        get_compressed_symbol_name_cursor(begin), out_name_buf
    );
}

struct string symbol_lookup_code_by_address(
    ptr_t address, char out_name_buf[static MAX_SYMBOL_LENGTH],
    size_t *out_offset_within, size_t *out_size
)
{
    if (unlikely(!address_is_kernel_code(address)))
        return NULL_STR();

    return do_lookup_symbol(
        address, out_name_buf, out_offset_within, out_size, false
    );
}

struct string symbol_lookup_data_by_address(
    ptr_t address, char out_name_buf[static MAX_SYMBOL_LENGTH],
    size_t *out_offset_within, size_t *out_size
)
{
    if (unlikely(!address_is_kernel_data(address)))
        return NULL_STR();

    return do_lookup_symbol(
        address, out_name_buf, out_offset_within, out_size, true
    );
}
