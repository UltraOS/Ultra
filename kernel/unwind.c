#define MSG_FMT(msg) "unwind: " msg


#include <arch/private/unwind.h>
#include <private/arch/unwind.h>
#include <private/unwind.h>
#include <private/symbols.h>

#include <log.h>
#include <unwind.h>
#include <symbols.h>
#include <leb128.h>

#include <common/string.h>
#include <common/attributes.h>

#define DWARF_SP_REG __builtin_dwarf_sp_column()

enum DW_EH_PE {
    // The lower 4 bits indicate the format of the data
    DW_EH_PE_absptr = 0x00,
    DW_EH_PE_uleb128 = 0x01,
    DW_EH_PE_udata2 = 0x02,
    DW_EH_PE_udata4 = 0x03,
    DW_EH_PE_udata8 = 0x04,

    // signed variants of the ops above, all have bit 3 set
#define DW_EH_PE_SIGNED_DATA (1 << 3)
    DW_EH_PE_sleb128 = DW_EH_PE_SIGNED_DATA | DW_EH_PE_uleb128,
    DW_EH_PE_sdata2 = DW_EH_PE_SIGNED_DATA | DW_EH_PE_udata2,
    DW_EH_PE_sdata4 = DW_EH_PE_SIGNED_DATA | DW_EH_PE_udata4,
    DW_EH_PE_sdata8 = DW_EH_PE_SIGNED_DATA | DW_EH_PE_udata8,

    // The upper 4 bits indicate how the value is to be applied.
    DW_EH_PE_pcrel = 0x10,
    DW_EH_PE_textrel = 0x20,
    DW_EH_PE_datarel = 0x30,
    DW_EH_PE_funcrel = 0x40,
    DW_EH_PE_aligned = 0x50,
};

static bool g_unwinder_available;

static const u8 *g_fde_binary_search_table;
static u8 g_fde_table_encoding;
static u8 g_fde_table_entry_width;
static u64 g_num_fdes;

#define EH_CONSUME_RAW_UNACCOUNTED(eh, dst, num_bytes) do { \
    memcpy(&(dst), (eh)->cursor, num_bytes);                \
    (eh)->cursor += num_bytes;                              \
} while (0)

#define EH_CONSUME_UNACCOUNTED(eh, dst, type) \
    EH_CONSUME_RAW_UNACCOUNTED(eh, dst, sizeof(type))

#define EH_CONSUME_RAW_UNSAFE(eh, dst, num_bytes) do { \
    EH_CONSUME_RAW_UNACCOUNTED(eh, dst, num_bytes);    \
    (eh)->bytes_left -= (num_bytes);                   \
} while (0)

#define EH_CONSUME_UNSAFE(eh, dst) do {                  \
    EH_CONSUME_RAW_UNSAFE(eh, dst, sizeof(typeof(dst))); \
} while (0)

#define eh_consume(eh, dst) ({                             \
    error_t ret = ERANGE;                                  \
                                                           \
    if (likely(sizeof(typeof(dst)) <= (eh)->bytes_left)) { \
        EH_CONSUME_UNSAFE(eh, dst);                        \
        ret = EOK;                                         \
    }                                                      \
                                                           \
    ret;                                                   \
})

static void eh_data_init(struct eh_data *eh, const u8 *cursor)
{
    eh->cursor = cursor;
    eh->bytes_left = 0;

    EH_CONSUME_UNACCOUNTED(eh, eh->bytes_left, u32);
    if (unlikely(eh->bytes_left == 0xFFFFFFFF))
        EH_CONSUME_UNACCOUNTED(eh, eh->bytes_left, u64);
}


static error_t decode_format(
    struct eh_data *data, u8 format, u64 *out_value
)
{
    error_t ret;
    u8 num_bytes, num_bits = 0;
    u64 value = 0;

    switch (format) {
    case DW_EH_PE_absptr:
        num_bytes = sizeof(ptr_t);
        goto out_copy_data;

    case DW_EH_PE_sleb128:
    case DW_EH_PE_uleb128: {
        u8 byte;

        do {
            ret = eh_consume(data, byte);
            if (is_error(ret))
                return ret;

            value |= (byte & LEB128_MAX_PER_BYTE) << num_bits;
            num_bits += LEB128_BITS_PER_BYTE;
        } while (byte & LEB128_HAS_NEXT_BYTE);

        goto out_no_copy;
    }

    case DW_EH_PE_udata8:
    case DW_EH_PE_sdata8:
        num_bytes = 8;
        goto out_copy_data;
    case DW_EH_PE_udata4:
    case DW_EH_PE_sdata4:
        num_bytes = 4;
        goto out_copy_data;
    case DW_EH_PE_udata2:
    case DW_EH_PE_sdata2:
        num_bytes = 2;
        goto out_copy_data;

    default:
        pr_warn("unhandled DWARF format %d\n", format);
        return ENOSYS;
    }

out_copy_data:
    if (unlikely(data->bytes_left < num_bytes))
        return EINVAL;

    EH_CONSUME_RAW_UNSAFE(data, value, num_bytes);

out_no_copy:
    // Sign-extend if needed
    if (format & DW_EH_PE_SIGNED_DATA) {
        if (!num_bits)
            num_bits = num_bytes * 8;

        num_bits--;

        if (value & (1ull << num_bits))
            value |= ~0ull << num_bits;
    }

    *out_value += value;
    return EOK;
}

static error_t decode_value(
    struct eh_data *data, const u8 encoding, u64 *out_value
)
{
    const u8 scaling = encoding & 0xF0;

    switch (scaling) {
    case DW_EH_PE_absptr:
        *out_value = 0;
        break;
    case DW_EH_PE_pcrel:
        *out_value = (ptr_t)data->cursor;
        break;
    case DW_EH_PE_textrel:
        *out_value = (ptr_t)g_linker_symbol_text_begin;
        break;
    case DW_EH_PE_datarel:
        *out_value = (ptr_t)g_linker_symbol_eh_frame_hdr_begin;
        break;
    case DW_EH_PE_funcrel:
    case DW_EH_PE_aligned:
    default:
        pr_warn("unhandled DWARF scaling %d\n", scaling);
        return ENOSYS;
    }

    return decode_format(data, encoding & 0xF, out_value);
}

ptr_t unwind_get_return_address(struct unwind_state *state)
{
    if (state->end)
        return 0;

    return state->frame[state->ret_reg_idx];
}

static ptr_t get_reliable_pc(struct unwind_state *state)
{
    ptr_t pc;

    pc = unwind_get_return_address(state);
    if (!state->signal_frame) {
        /*
         * Recovered PC points to after the call instruction, which might be
         * past the end of this function. Subtract one to make sure we're
         * inside the bounds of the caller.
         */
        pc -= 1;
    }

    return pc;
}

error_t unwind_init(void)
{
    error_t ret;
    struct eh_data data = {
        .cursor = g_linker_symbol_eh_frame_hdr_begin,
        .bytes_left = g_linker_symbol_eh_frame_hdr_end -
                      g_linker_symbol_eh_frame_hdr_begin,
    };
    u8 version, eh_frame_ptr_encoding, fde_count_encoding;
    u64 value;

    /*
     * Format of .eh_frame_hdr:
     * Encoding      | Field
     * --------------|-------------------
     * unsigned byte | version
     * unsigned byte | eh_frame_ptr_enc
     * unsigned byte | fde_count_enc
     * unsigned byte | table_enc
     * encoded       | eh_frame_ptr
     * encoded       | fde_count
     * encoded       | binary search table
     */

    ret = eh_consume(&data, version);
    if (is_error(ret))
        return ret;
    if (unlikely(version != 1))
        return EINVAL;

    ret = eh_consume(&data, eh_frame_ptr_encoding);
    if (is_error(ret))
        return ret;

    ret = eh_consume(&data, fde_count_encoding);
    if (is_error(ret))
        return ret;

    ret = eh_consume(&data, g_fde_table_encoding);
    if (is_error(ret))
        return ret;

    ret = decode_value(&data, eh_frame_ptr_encoding, &value);
    if (is_error(ret))
        return ret;

    if (unlikely((ptr_t)g_linker_symbol_eh_frame_begin != value))
        return EINVAL;

    ret = decode_value(&data, fde_count_encoding, &g_num_fdes);
    if (is_error(ret))
        return ret;

    if (unlikely(g_num_fdes > g_symbol_count))
        return EINVAL;

    g_fde_binary_search_table = data.cursor;

    ret = decode_value(&data, g_fde_table_encoding, &value);
    if (is_error(ret))
        return ret;

    /*
     * Assume all BST values have the same width (there's nothing else we can
     * do here that would make sense). Simply check how many bytes were
     * consumed to decode the first value in the table.
     */
    g_fde_table_entry_width = data.cursor - g_fde_binary_search_table;

    // Multiply by 2 because each entry is made up of two values
    g_fde_table_entry_width *= 2;

    pr_info("stack traces are available!\n");
    g_unwinder_available = true;
    return EOK;
}

static ptr_or_error_t find_fde(ptr_t pc)
{
    error_t ret;
    u64 sym_addr, fde_addr;
    u64 i, begin = 0, end = g_num_fdes;
    struct eh_data data;

    while (end - begin > 1) {
        i = begin + ((end - begin) / 2);

        data = (struct eh_data) {
            .cursor = &g_fde_binary_search_table[i * g_fde_table_entry_width],
            .bytes_left = g_fde_table_entry_width,
        };
        ret = decode_value(&data, g_fde_table_encoding, &sym_addr);
        if (is_error(ret))
            return encode_error_ptr(ret);

        if (sym_addr <= pc)
            begin = i;
        else
            end = i;
    }

    data = (struct eh_data) {
        .cursor = &g_fde_binary_search_table[begin * g_fde_table_entry_width]
                  + g_fde_table_entry_width / 2,
        .bytes_left = g_fde_table_entry_width / 2,
    };
    ret = decode_value(&data, g_fde_table_encoding, &fde_addr);
    if (is_error(ret))
        return encode_error_ptr(ret);

    return (void*)((ptr_t)fde_addr);
}

ERROR_EMITTER(
    "must be invoked from arch-specific arch_unwind_current_begin "
    "assembly stub"
)
error_t unwind_current_begin(struct unwind_state *state, ptr_t starting_pc)
{
    error_t ret;

    /*
     * We have to unwind until the caller of the caller of unwind_begin,
     * otherwise we will be unwinding already freed stack frames.
     */
    do {
        ret = unwind_next_frame(state);
    } while (
        !unwind_is_done(state) &&
        unwind_get_return_address(state) != starting_pc
    );

    return ret;
}

error_t unwind_begin(
    struct unwind_state *state, struct registers *regs, ptr_t starting_pc
)
{
    error_t ret = EOK;

    if (unlikely(!g_unwinder_available))
        return ENODEV;

    memzero(state, sizeof(*state));

    state->ret_reg_idx = ARCH_DWARF_PC_REG;

    if (regs == NULL) {
        ret = arch_unwind_current_begin(state, starting_pc);
    } else {
        arch_registers_to_dwarf_registers(regs, state->frame);
        state->signal_frame = true;
    }

    return ret;
}

bool unwind_is_done(const struct unwind_state *state)
{
    return state->end;
}

/*
 * We expect the following augmentation value:
 * - z: has augmentation data
 * - R: pointer encoding stored in aug data
 * - \0: end of augmentation string
 */
#define EXPECTED_AUG_STRING "zR"

static error_t parse_cie(struct unwind_state *state, struct eh_data *cie)
{
    error_t ret;
    u32 id;
    size_t aug_idx;
    u64 aug_length, ret_reg;
    char aug_ch;
    u8 version;

    ret = eh_consume(cie, id);
    if (is_error(ret))
        return ret;
    if (unlikely(id != 0))
        return EINVAL;

    ret = eh_consume(cie, version);
    if (is_error(ret))
        return ret;
    if (unlikely(version != 1))
        return EINVAL;

    for (aug_idx = 0; aug_idx < sizeof(EXPECTED_AUG_STRING); aug_idx++) {
        ret = eh_consume(cie, aug_ch);
        if (is_error(ret))
            return ret;

        if (unlikely(aug_ch != EXPECTED_AUG_STRING[aug_idx])) {
            pr_warn(
                "unhandled DWARF augmentation @%zu: '%c'\n", aug_idx, aug_ch
            );
            return ENOSYS;
        }
    }

    ret = decode_value(cie, DW_EH_PE_uleb128, &state->code_alignment_factor);
    if (is_error(ret))
        return ret;

    ret = decode_value(
        cie, DW_EH_PE_sleb128, (u64*)&state->data_alignment_factor
    );
    if (is_error(ret))
        return ret;

    ret = decode_value(cie, DW_EH_PE_uleb128, &ret_reg);
    if (is_error(ret))
        return ret;
    if (unlikely(ret_reg >= ARCH_NUM_DWARF_REGISTERS))
        return EINVAL;
    state->ret_reg_idx = ret_reg;

    ret = decode_value(cie, DW_EH_PE_uleb128, &aug_length);
    if (is_error(ret))
        return ret;
    if (unlikely(aug_length != 1))
        return EINVAL;

    ret = eh_consume(cie, state->fde_encoding);
    if (is_error(ret))
        return ret;

    state->cie_code = *cie;
    return EOK;
}

error_t parse_fde(struct unwind_state *state, struct eh_data *fde)
{
    error_t ret;
    struct eh_data cie;
    u32 cie_offset;
    ptr_t current_pc;
    u64 pc_size;
    u8 aug_length;

    /*
     * Find the CIE of this FDE. It's stored as a 32-bit offset from the start
     * of the field itself.
     */
    ret = eh_consume(fde, cie_offset);
    if (is_error(ret))
        return ret;

    // Must not be 0 for FDEs
    if (unlikely(cie_offset == 0))
        return EINVAL;

    eh_data_init(&cie, fde->cursor - cie_offset - sizeof(cie_offset));
    ret = parse_cie(state, &cie);
    if (is_error(ret))
        return ret;

    ret = decode_value(fde, state->fde_encoding, &state->pc);
    if (is_error(ret))
        return ret;

    /*
     * This field is encoded in a magic way, with different software using
     * different heuristics to decode it. The most common way is to treat it
     * as being encoded in the same way as 'pc_begin', but absolute (aka with
     * top 4 bits of encoding masked).
     */
    ret = decode_value(fde, state->fde_encoding & 0x0F, &pc_size);
    if (is_error(ret))
        return ret;

    current_pc = get_reliable_pc(state);
    if (unlikely(current_pc < state->pc ||
                 current_pc >= (state->pc + pc_size)))
        return EINVAL;

    ret = eh_consume(fde, aug_length);
    if (is_error(ret))
        return ret;
    if (unlikely(aug_length != 0))
        return ENOSYS;

    state->fde_code = *fde;
    return EOK;
}

static error_t prepare_unwind_state(struct unwind_state *state)
{
    ptr_or_error_t pret;
    struct eh_data fde;

    pret = find_fde(get_reliable_pc(state));
    if (error_ptr(pret))
        return decode_error_ptr(pret);

    eh_data_init(&fde, pret);

    return parse_fde(state, &fde);
}

#define HIGH_2_BITS_OP(x) ((x) << 6)
#define LOW_6_BITS(x) ((x) & ((1 << 6) - 1))
#define AS_HIGH_2_BITS_OP(x) ((x) & ~((1 << 6) - 1))

enum dw_cfa_opcode {
    DW_CFA_nop = 0x00,
    DW_CFA_advance_loc1 = 0x02,
    DW_CFA_advance_loc2 = 0x03,
    DW_CFA_advance_loc4 = 0x04,
    DW_CFA_same_value = 0x08,
    DW_CFA_def_cfa = 0x0C,
    DW_CFA_def_cfa_register = 0x0D,
    DW_CFA_def_cfa_offset = 0x0E,
    DW_CFA_advance_loc = HIGH_2_BITS_OP(0x01),
    DW_CFA_offset = HIGH_2_BITS_OP(0x02),
};

struct register_rule {
    enum dw_cfa_opcode rule;
    u64 offset;
};

error_t dwarf_exec(
    struct unwind_state *state, struct register_rule *rules,
    struct eh_data *data
)
{
    error_t ret;
    u8 opcode;
    u64 value;

    while (data->bytes_left) {
        if (state->pc > unwind_get_return_address(state))
            return EOK;

        ret = eh_consume(data, opcode);
        if (is_error(ret))
            return ret;

        switch (AS_HIGH_2_BITS_OP(opcode))
        {
        case 0x00:
            break;
        case DW_CFA_advance_loc:
            state->pc += LOW_6_BITS(opcode) * state->code_alignment_factor;
            continue;

        case DW_CFA_offset: {
            u8 reg = LOW_6_BITS(opcode);

            if (unlikely(reg >= ARCH_NUM_DWARF_REGISTERS))
                return EINVAL;

            ret = decode_value(data, DW_EH_PE_uleb128, &rules[reg].offset);
            if (is_error(ret))
                return ret;

            rules[reg].rule = DW_CFA_offset;
            continue;
        }
        default:
            goto out_unhandled;
        }

        switch (opcode) {
        case DW_CFA_nop:
            continue;

        case DW_CFA_advance_loc4:
            opcode += 1; // makes math below easier
            FALLTHROUGH;
        case DW_CFA_advance_loc2:
        case DW_CFA_advance_loc1: {
            u8 bytes_needed = opcode - 1;
            u32 delta = 0;

            if (unlikely(data->bytes_left < bytes_needed))
                return EINVAL;

            EH_CONSUME_RAW_UNSAFE(data, delta, bytes_needed);
            state->pc += delta * state->code_alignment_factor;
            continue;
        }

        case DW_CFA_def_cfa:
        case DW_CFA_def_cfa_register:
            ret = decode_value(data, DW_EH_PE_uleb128, &value);
            if (is_error(ret))
                return ret;
            if (unlikely(value >= ARCH_NUM_DWARF_REGISTERS))
                return EINVAL;

            state->cfa_reg_idx = value;
            if (opcode != DW_CFA_def_cfa)
                continue;

            ret = decode_value(data, DW_EH_PE_uleb128, &state->cfa_offset);
            if (is_error(ret))
                return ret;

            continue;

        case DW_CFA_def_cfa_offset:
            ret = decode_value(data, DW_EH_PE_uleb128, &state->cfa_offset);
            if (is_error(ret))
                return ret;
            continue;

        default:
            goto out_unhandled;
        }
    }

    return EOK;

out_unhandled:
    pr_warn("unhandled DWARF CFA opcode 0x%02X\n", opcode);
    return ENOSYS;
}

static error_t apply_reg_rules(
    struct unwind_state *state, ptr_t *new_frame,
    struct register_rule *reg_rules
)
{
    size_t i;
    u64 offset;

    for (i = 0; i < ARCH_NUM_DWARF_REGISTERS; i++) {
        switch (reg_rules[i].rule) {
        case DW_CFA_same_value:
            new_frame[i] = state->frame[i];
            continue;
        case DW_CFA_offset:
            offset = state->frame[state->cfa_reg_idx] + state->cfa_offset;
            offset += reg_rules[i].offset * state->data_alignment_factor;
            memcpy(&new_frame[i], (void*)((ptr_t)offset), sizeof(ptr_t));
            continue;
        case DW_CFA_def_cfa:
            new_frame[i] = state->frame[state->cfa_reg_idx] + state->cfa_offset;
            continue;
        default:
            return ENOSYS;
        }
    }

    state->cfa_reg_idx = DWARF_SP_REG;
    state->cfa_offset = 0;
    return EOK;
}

error_t unwind_next_frame(struct unwind_state *state)
{
    error_t ret;
    size_t i;
    ptr_t new_frame[ARCH_NUM_DWARF_REGISTERS];
    struct register_rule reg_rules[ARCH_NUM_DWARF_REGISTERS];

    if (unlikely(state->end))
        return EINVAL;

    ret = prepare_unwind_state(state);
    if (is_error(ret))
        goto out_error;

    for (i = 0; i < ARCH_NUM_DWARF_REGISTERS; i++) {
        if (i == DWARF_SP_REG) {
            reg_rules[i].rule = DW_CFA_def_cfa;
            continue;
        }
        reg_rules[i].rule = DW_CFA_same_value;
    }
    state->signal_frame = false;

    ret = dwarf_exec(state, reg_rules, &state->cie_code);
    if (is_error(ret))
        goto out_error;

    ret = dwarf_exec(state, reg_rules, &state->fde_code);
    if (is_error(ret))
        goto out_error;

    ret = apply_reg_rules(state, new_frame, reg_rules);
    if (is_error(ret))
        goto out_error;

    memcpy(state->frame, new_frame, sizeof(state->frame));
    state->end = unwind_get_return_address(state) == 0;
    return EOK;

out_error:
    state->end = true;
    return ret;
}

error_t unwind_walk(struct registers *regs, unwind_cb_t callback, void *user)
{
    error_t ret;
    ptr_t starting_pc = 0;
    struct unwind_state state;

    if (regs == NULL)
        starting_pc = (ptr_t)__builtin_return_address(0);

    ret = unwind_begin(&state, regs, starting_pc);
    if (is_error(ret))
        return ret;

    do {
        ptr_t ret_addr;

        ret_addr = unwind_get_return_address(&state);
        if (!callback(user, ret_addr, !state.signal_frame))
           break;

        ret = unwind_next_frame(&state);
    } while (!unwind_is_done(&state));

    return ret;
}
