#define MSG_FMT(msg) "ubsan: " msg

#include <common/atomic.h>
#include <common/bit.h>
#include <common/format.h>
#include <common/types.h>

#include <log.h>
#include <panic.h>

#include <private/bug.h>
#include <private/ubsan.h>

#define UBSAN_MAX_INT_WIDTH BITS_PER_TYPE(u128)
#define UBSAN_COLUMN_REPORTED UINT32_MAX
#define UBSAN_VALUE_STR_SIZE 48

// TODO: move this into struct task once we have it
static bool s_in_report;

static u32 type_bit_width(const struct ubsan_type *type)
{
    return BIT_U32(BIT_FIELD_READ(type->info, UBSAN_TYPE_INFO_LOG2_WIDTH));
}

static bool type_is_int(const struct ubsan_type *type)
{
    return type->kind == UBSAN_TYPE_KIND_INT &&
           type_bit_width(type) <= UBSAN_MAX_INT_WIDTH;
}

static bool type_is_signed(const struct ubsan_type *type)
{
    return type_is_int(type) && (type->info & UBSAN_TYPE_INFO_SIGNED);
}

// Values wider than a pointer are passed by reference
static bool value_is_inline(const struct ubsan_type *type)
{
    return type_bit_width(type) <= BITS_PER_TYPE(ptr_t);
}

static u128 value_unsigned(const struct ubsan_type *type, ptr_t value)
{
    u32 extra_bits;

    if (!type_is_int(type))
        return 0;
    if (!value_is_inline(type))
        return *(const u128*)value;

    extra_bits = UBSAN_MAX_INT_WIDTH - type_bit_width(type);
    return ((u128)value << extra_bits) >> extra_bits;
}

static i128 value_signed(const struct ubsan_type *type, ptr_t value)
{
    u32 extra_bits;

    if (!type_is_int(type))
        return 0;
    if (!value_is_inline(type))
        return *(const i128*)value;

    extra_bits = UBSAN_MAX_INT_WIDTH - type_bit_width(type);
    return (i128)((u128)value << extra_bits) >> extra_bits;
}

static void format_value(
    char *buf, size_t size, const struct ubsan_type *type, ptr_t value
)
{
    i128 sval;
    u128 uval;

    if (!type_is_int(type)) {
        snprintf(buf, size, "<unknown>");
        return;
    }

    if (type_is_signed(type)) {
        sval = value_signed(type, value);

        if (sval >= INT64_MIN && sval <= INT64_MAX) {
            snprintf(buf, size, "%lld", (i64)sval);
            return;
        }
    }

    uval = value_unsigned(type, value);

    if (uval <= UINT64_MAX) {
        snprintf(buf, size, "%llu", (u64)uval);
        return;
    }

    snprintf(
        buf, size, "0x%016llX%016llX", (u64)(uval >> BITS_PER_TYPE(u64)),
        (u64)uval
    );
}

static bool report_begin(struct ubsan_source_location *loc, const char *check)
{
    u32 column;

    if (atomic_xchg(&s_in_report, true, MO_ACQ_REL))
        return false;

    /*
     * The compiler emits the location as writable data, reuse the column
     * field as the per-site "reported" flag.
     */
    column = atomic_xchg(&loc->column, UBSAN_COLUMN_REPORTED, MO_RELAXED);
    if (column == UBSAN_COLUMN_REPORTED) {
        atomic_store_release(&s_in_report, false);
        return false;
    }

    pr_warn("%s in %s:%u:%u\n", check, loc->file, loc->line, column);
    return true;
}

static void report_end(void)
{
    finish_warn_report(nullptr);
    atomic_store_release(&s_in_report, false);
}

void __ubsan_handle_out_of_bounds(
    struct ubsan_out_of_bounds_data *data, ptr_t index
)
{
    char index_str[UBSAN_VALUE_STR_SIZE];

    if (!report_begin(&data->loc, "array-index-out-of-bounds"))
        return;

    format_value(index_str, sizeof(index_str), data->index_type, index);
    pr_warn(
        "index %s is out of range for type %s\n", index_str,
        data->array_type->name
    );

    report_end();
}

void __ubsan_handle_shift_out_of_bounds(
    struct ubsan_shift_data *data, ptr_t lhs, ptr_t rhs
)
{
    char lhs_str[UBSAN_VALUE_STR_SIZE], rhs_str[UBSAN_VALUE_STR_SIZE];
    const struct ubsan_type *lhs_type = data->lhs_type;
    const struct ubsan_type *rhs_type = data->rhs_type;

    if (!report_begin(&data->loc, "shift-out-of-bounds"))
        return;

    format_value(lhs_str, sizeof(lhs_str), lhs_type, lhs);
    format_value(rhs_str, sizeof(rhs_str), rhs_type, rhs);

    if (type_is_signed(rhs_type) && value_signed(rhs_type, rhs) < 0)
        pr_warn("shift exponent %s is negative\n", rhs_str);
    else if (value_unsigned(rhs_type, rhs) >= type_bit_width(lhs_type))
        pr_warn(
            "shift exponent %s is too large for %u-bit type %s\n", rhs_str,
            type_bit_width(lhs_type), lhs_type->name
        );
    else if (type_is_signed(lhs_type) && value_signed(lhs_type, lhs) < 0)
        pr_warn("left shift of negative value %s\n", lhs_str);
    else
        pr_warn(
            "left shift of %s by %s places cannot be represented in type %s\n",
            lhs_str, rhs_str, lhs_type->name
        );

    report_end();
}

static void handle_overflow(
    struct ubsan_value_data *data, ptr_t lhs, ptr_t rhs, char op
)
{
    char lhs_str[UBSAN_VALUE_STR_SIZE], rhs_str[UBSAN_VALUE_STR_SIZE];

    if (!report_begin(&data->loc, "integer-overflow"))
        return;

    format_value(lhs_str, sizeof(lhs_str), data->type, lhs);
    format_value(rhs_str, sizeof(rhs_str), data->type, rhs);
    pr_warn(
        "%s %c %s cannot be represented in type %s\n", lhs_str, op, rhs_str,
        data->type->name
    );

    report_end();
}

void __ubsan_handle_add_overflow(
    struct ubsan_value_data *data, ptr_t lhs, ptr_t rhs
)
{
    handle_overflow(data, lhs, rhs, '+');
}

void __ubsan_handle_sub_overflow(
    struct ubsan_value_data *data, ptr_t lhs, ptr_t rhs
)
{
    handle_overflow(data, lhs, rhs, '-');
}

void __ubsan_handle_mul_overflow(
    struct ubsan_value_data *data, ptr_t lhs, ptr_t rhs
)
{
    handle_overflow(data, lhs, rhs, '*');
}

void __ubsan_handle_negate_overflow(
    struct ubsan_value_data *data, ptr_t value
)
{
    char value_str[UBSAN_VALUE_STR_SIZE];

    if (!report_begin(&data->loc, "integer-overflow"))
        return;

    format_value(value_str, sizeof(value_str), data->type, value);
    pr_warn(
        "negation of %s cannot be represented in type %s\n", value_str,
        data->type->name
    );

    report_end();
}

void __ubsan_handle_divrem_overflow(
    struct ubsan_value_data *data, ptr_t lhs, ptr_t rhs
)
{
    char lhs_str[UBSAN_VALUE_STR_SIZE];

    if (!report_begin(&data->loc, "division-overflow"))
        return;

    if (type_is_signed(data->type) && value_signed(data->type, rhs) == -1) {
        format_value(lhs_str, sizeof(lhs_str), data->type, lhs);
        pr_warn(
            "division of %s by -1 cannot be represented in type %s\n",
            lhs_str, data->type->name
        );
    } else {
        pr_warn("division by zero\n");
    }

    report_end();
}

void __ubsan_handle_load_invalid_value(
    struct ubsan_value_data *data, ptr_t value
)
{
    char value_str[UBSAN_VALUE_STR_SIZE];

    if (!report_begin(&data->loc, "invalid-load"))
        return;

    format_value(value_str, sizeof(value_str), data->type, value);
    pr_warn(
        "load of value %s is not a valid value for type %s\n", value_str,
        data->type->name
    );

    report_end();
}

void __ubsan_handle_builtin_unreachable(struct ubsan_unreachable_data *data)
{
    panic(
        "ubsan: reached unreachable code in %s:%u:%u", data->loc.file,
        data->loc.line, data->loc.column
    );
}
