#pragma once

#include <common/attributes.h>
#include <common/bit.h>
#include <common/types.h>

#define UBSAN_TYPE_KIND_INT 0
#define UBSAN_TYPE_INFO_SIGNED BIT_U16(0)
#define UBSAN_TYPE_INFO_LOG2_WIDTH MAKE_BIT_MASK_U16(15, 1)

struct ubsan_source_location {
    const char *file;
    u32 line;
    u32 column;
};

struct ubsan_type {
    u16 kind;
    u16 info;
    char name[];
};

struct ubsan_out_of_bounds_data {
    struct ubsan_source_location loc;
    const struct ubsan_type *array_type;
    const struct ubsan_type *index_type;
};

struct ubsan_shift_data {
    struct ubsan_source_location loc;
    const struct ubsan_type *lhs_type;
    const struct ubsan_type *rhs_type;
};

struct ubsan_value_data {
    struct ubsan_source_location loc;
    const struct ubsan_type *type;
};

struct ubsan_unreachable_data {
    struct ubsan_source_location loc;
};

void __ubsan_handle_out_of_bounds(
    struct ubsan_out_of_bounds_data *data, ptr_t index
);
void __ubsan_handle_shift_out_of_bounds(
    struct ubsan_shift_data *data, ptr_t lhs, ptr_t rhs
);
void __ubsan_handle_add_overflow(
    struct ubsan_value_data *data, ptr_t lhs, ptr_t rhs
);
void __ubsan_handle_sub_overflow(
    struct ubsan_value_data *data, ptr_t lhs, ptr_t rhs
);
void __ubsan_handle_mul_overflow(
    struct ubsan_value_data *data, ptr_t lhs, ptr_t rhs
);
void __ubsan_handle_negate_overflow(
    struct ubsan_value_data *data, ptr_t value
);
void __ubsan_handle_divrem_overflow(
    struct ubsan_value_data *data, ptr_t lhs, ptr_t rhs
);
void __ubsan_handle_load_invalid_value(
    struct ubsan_value_data *data, ptr_t value
);

NORETURN
void __ubsan_handle_builtin_unreachable(struct ubsan_unreachable_data *data);
