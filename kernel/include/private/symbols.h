#include <common/types.h>

// Number of kernel symbols
extern const u32 g_symbol_count;

/*
 * An array of 'g_symbol_count' sub arrays where each array encodes a symbol
 * name. Each array is made up of the following data:
 *     - [u8] LEB128 length of the compressed token stream below
 *     - [u8] an array of indices into 'g_symbol_token_table'
 * The final name is then the result of concatenating every token specified
 * in the stream above.
 */
extern const u8 g_symbol_compressed_names[];

/*
 * An array of 'g_symbol_count' / 256 entries, where each entry specifies the
 * offset into 'g_symbol_compressed_names' where the name at index & 0xFF can
 * be found. This makes name from index lookup O(1) instead of O(N).
 */
extern const u32 g_symbol_name_offsets[];

/*
 * An array of 256 null-terminated ASCII strings that make up the kernel
 * symbols, each token at 'i' starts at the offset 'g_symbol_token_offsets[i]'.
 * The tokens in 'g_symbol_compressed_names' are actually indices into this
 * table, a token can be identity mapped, e.g. 0x41 -> ['A', '\0'], but the
 * interning algorithm in scripts/generate_symbol_tables.py tries to store
 * the most commonly occurring tokens as one multibyte string, for example
 * token 0x78 could map to the string "uacpi_" thus compressing all symbols
 * containing the token "uacpi_" from ['u', 'a', 'c', 'p', 'i', '_'] to just
 * 0x78.
 */
extern const char g_symbol_token_table[];
extern const u16 g_symbol_token_offsets[];

/*
 * An array of 'g_symbol_count' symbol addresses relative to 'g_symbol_base'
 * sorted in ascending order. Note that both this table and
 * 'g_symbol_compressed_names' are sorted in the same order.
 */
extern const u32 g_symbol_relative_addresses[];
extern const ptr_t g_symbol_base;

/*
 * An array of 'g_symbol_count' 3-byte indices of names sorted in
 * lexicographical order that map names to their respective index in
 * 'g_symbol_relative_addresses' as well as the 'g_symbol_compressed_names'
 * tables. This can be used for O(log N) lookups of name -> address.
 */
extern const u8 g_symbol_name_index_to_address_index[];
