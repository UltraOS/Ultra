#pragma once

#include <common/error.h>
#include <common/types.h>

#include <arch/registers.h>
#include <arch/private/unwind.h>

struct eh_data {
    const u8 *cursor;
    u64 bytes_left;
};

struct unwind_state {
    ptr_t frame[ARCH_NUM_DWARF_REGISTERS];

    u64 code_alignment_factor;
    i64 data_alignment_factor;

    u64 cfa_offset;

    u64 pc;
    u64 pc_end;

    u8 ret_reg_idx;
    u8 cfa_reg_idx;

    bool end;
    bool signal_frame;

    u8 fde_encoding;

    struct eh_data cie_code;
    struct eh_data fde_code;
};

error_t unwind_init(void);

error_t unwind_begin(struct unwind_state*, struct registers*, ptr_t starting_pc);

bool unwind_is_done(const struct unwind_state*);

error_t unwind_next_frame(struct unwind_state*);

ptr_t unwind_get_return_address(struct unwind_state*);
