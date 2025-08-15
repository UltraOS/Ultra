#include <common/types.h>
#include <common/format.h>
#include <common/minmax.h>
#include <common/string.h>
#include <common/string_container.h>
#include <common/ctype.h>
#include <common/conversions.h>
#include <common/error.h>

struct fmt_buf_state {
    char *buffer;
    size_t capacity;
    size_t bytes_written;
};

struct fmt_spec {
    u8 is_signed      : 1;
    u8 prepend        : 1;
    u8 uppercase      : 1;
    u8 left_justify   : 1;
    u8 alternate_form : 1;
    u8 has_precision  : 1;
    char pad_char;
    char prepend_char;
    u64 min_width;
    u64 precision;
    u32 base;
};

static void write_one(struct fmt_buf_state *fb_state, char c)
{
    if (fb_state->bytes_written < fb_state->capacity)
        fb_state->buffer[fb_state->bytes_written] = c;

    fb_state->bytes_written++;
}

static void write_many(
    struct fmt_buf_state *fb_state, const char *string, size_t count
)
{
    if (fb_state->bytes_written < fb_state->capacity) {
        size_t count_to_write;

        count_to_write = MIN(
            count, fb_state->capacity - fb_state->bytes_written
        );
        memcpy(
            &fb_state->buffer[fb_state->bytes_written], string, count_to_write
        );
    }

    fb_state->bytes_written += count;
}

static char hex_char(bool upper, u64 value)
{
    static const char upper_hex[] = "0123456789ABCDEF";
    static const char lower_hex[] = "0123456789abcdef";

    return (upper ? upper_hex : lower_hex)[value];
}

static void write_padding(
    struct fmt_buf_state *fb_state, struct fmt_spec *fm, size_t repr_size
)
{
    u64 mw = fm->min_width;

    if (mw <= repr_size)
        return;

    mw -= repr_size;

    while (mw--)
        write_one(fb_state, fm->left_justify ? ' ' : fm->pad_char);
}

#define REPR_BUFFER_SIZE 32

static void write_integer(
    struct fmt_buf_state *fb_state, struct fmt_spec *fm, u64 value
)
{
    char repr_buffer[REPR_BUFFER_SIZE];
    size_t index = REPR_BUFFER_SIZE;
    u64 remainder;
    char repr;
    bool negative = false;
    size_t repr_size;

    if (fm->is_signed) {
        i64 as_ll = value;

        if (as_ll < 0) {
            value = -as_ll;
            negative = true;
        }
    }

    if (fm->prepend || negative)
        write_one(fb_state, negative ? '-' : fm->prepend_char);

    while (value) {
        remainder = value % fm->base;
        value /= fm->base;

        if (fm->base == 16) {
            repr = hex_char(fm->uppercase, remainder);
        } else if (fm->base == 8 || fm->base == 10) {
            repr = remainder + '0';
        } else {
            repr = '?';
        }

        repr_buffer[--index] = repr;
    }
    repr_size = REPR_BUFFER_SIZE - index;

    if (repr_size == 0) {
        repr_buffer[--index] = '0';
        repr_size = 1;
    }

    if (fm->alternate_form) {
        if (fm->base == 16) {
            repr_buffer[--index] = fm->uppercase ? 'X' : 'x';
            repr_buffer[--index] = '0';
            repr_size += 2;
        } else if (fm->base == 8) {
            repr_buffer[--index] = '0';
            repr_size += 1;
        }
    }

    if (fm->left_justify) {
        write_many(fb_state, &repr_buffer[index], repr_size);
        write_padding(fb_state, fm, repr_size);
    } else {
        write_padding(fb_state, fm, repr_size);
        write_many(fb_state, &repr_buffer[index], repr_size);
    }
}

static void consume_digits(struct string *fmt, struct string *out_digits)
{
    out_digits->text = fmt->text;
    out_digits->size = 0;

    while (!str_empty(*fmt)) {
        if (!isdigit(fmt->text[0]))
            return;

        str_extend_by(out_digits, 1);
        str_offset_by(fmt, 1);
    }
}

enum parse_number_mode {
    PARSE_NUMBER_MODE_MAYBE,
    PARSE_NUMBER_MODE_MUST,
};

static error_t parse_number(
    struct string *fmt, enum parse_number_mode mode, u64 *out_value
)
{
    struct string digits;

    consume_digits(fmt, &digits);
    if (str_empty(digits))
        return mode == PARSE_NUMBER_MODE_MUST ? EINVAL : EOK;

    return str_to_u64_with_base(digits, out_value, 10);
}

static bool consume(struct string *fmt, struct string tok)
{
    if (!str_starts_with(*fmt, tok))
        return false;

    str_offset_by(fmt, tok.size);
    return true;
}

static bool consume_one_of(
    struct string *str, struct string tok_list, char *consumed_char
)
{
    ssize_t i;

    if (str_empty(*str))
        return false;

    i = str_find_one(tok_list, str->text[0], 0);
    if (i < 0)
        return false;

    *consumed_char = tok_list.text[i];
    str_offset_by(str, 1);
    return true;
}

static u32 base_from_specifier(char specifier)
{
    switch (specifier)
    {
        case 'x':
        case 'X':
            return 16;
        case 'o':
            return 8;
        default:
            return 10;
    }
}

static bool is_uppercase_specifier(char specifier)
{
    return specifier == 'X';
}

MAYBE_NERR(int) vsnprintf(
    char *buffer, size_t capacity, const char *fmt_str,
    va_list vlist
)
{
    struct fmt_buf_state fb_state = { 0 };
    struct string fmt;
    u64 value;
    ssize_t next_offset;
    char flag;

    fmt = STR(fmt_str);

    fb_state.buffer = buffer;
    fb_state.capacity = capacity;
    fb_state.bytes_written = 0;

    while (!str_empty(fmt)) {
        struct fmt_spec fm = {
            .pad_char = ' ',
            .base = 10,
        };
        next_offset = str_find_one(fmt, '%', 0);
        if (next_offset < 0)
            next_offset = fmt.size;

        if (next_offset)
            write_many(&fb_state, fmt.text, next_offset);

        str_offset_by(&fmt, next_offset);
        if (str_empty(fmt))
            break;

        if (consume(&fmt, STR("%%"))) {
            write_one(&fb_state, '%');
            continue;
        }

        // consume %
        str_offset_by(&fmt, 1);

        while (consume_one_of(&fmt, STR("+- 0#"), &flag)) {
            switch (flag) {
            case '+':
            case ' ':
                fm.prepend = true;
                fm.prepend_char = flag;
                continue;
            case '-':
                fm.left_justify = true;
                continue;
            case '0':
                fm.pad_char = '0';
                continue;
            case '#':
                fm.alternate_form = true;
                continue;
            default:
                return -EINVAL;
            }
        }

        if (consume(&fmt, STR("*"))) {
            fm.min_width = va_arg(vlist, int);
        } else {
            error_t ret;

            ret = parse_number(&fmt, PARSE_NUMBER_MODE_MAYBE, &fm.min_width);
            if (is_error(ret))
                return -ret;
        }

        if (consume(&fmt, STR("."))) {
            fm.has_precision = true;

            if (consume(&fmt, STR("*"))) {
                fm.precision = va_arg(vlist, int);
            } else {
                error_t ret;

                ret = parse_number(&fmt, PARSE_NUMBER_MODE_MUST, &fm.precision);
                if (is_error(ret))
                    return -ret;
            }
        }

        flag = 0;

        if (consume(&fmt, STR("c"))) {
            char c = va_arg(vlist, int);
            write_one(&fb_state, c);
            continue;
        }

        if (consume(&fmt, STR("s"))) {
            const char *string = va_arg(vlist, char*);
            size_t i;

            if (unlikely(string == NULL))
                string = "<null>";

            for (i = 0; (!fm.has_precision || i < fm.precision) && string[i]; i++)
                write_one(&fb_state, string[i]);
            while (i++ < fm.min_width)
                write_one(&fb_state, ' ');
            continue;
        }

        if (consume(&fmt, STR("p"))) {
            if (consume(&fmt, STR("S"))) {
                size_t size;
                struct string *string = va_arg(vlist, struct string*);

                if (WARN_ON(string == NULL)) {
                    static struct string null_string = STR("<null-string>");
                    string = &null_string;
                }

                size = string->size;
                if (fm.has_precision)
                    size = MIN(fm.precision, string->size);

                write_many(&fb_state, string->text, size);
                while (size < fm.precision)
                    write_one(&fb_state, ' ');
                continue;
            }

            value = (ptr_t)va_arg(vlist, void*);
            fm.base = 16;
            fm.min_width = ULTRA_ARCH_WIDTH * 2;
            fm.pad_char = '0';
            goto write_int;
        }

        if (consume(&fmt, STR("hh"))) {
            if (consume(&fmt, STR("d")) || consume(&fmt, STR("i"))) {
                value = (signed char)va_arg(vlist, int);
                fm.is_signed = true;
            } else if (consume_one_of(&fmt, STR("oxXu"), &flag)) {
                value = (unsigned char)va_arg(vlist, int);
            } else {
                return -EINVAL;
            }
            goto write_int;
        }

        if (consume(&fmt, STR("h"))) {
            if (consume(&fmt, STR("d")) || consume(&fmt, STR("i"))) {
                value = (signed short)va_arg(vlist, int);
                fm.is_signed = true;
            } else if (consume_one_of(&fmt, STR("oxXu"), &flag)) {
                value = (unsigned short)va_arg(vlist, int);
            } else {
                return -EINVAL;
            }
            goto write_int;
        }

        if (consume(&fmt, STR("ll")) ||
            (sizeof(size_t) == sizeof(long long) && consume(&fmt, STR("z")))) {
            if (consume(&fmt, STR("d")) || consume(&fmt, STR("i"))) {
                value = va_arg(vlist, long long);
                fm.is_signed = true;
            } else if (consume_one_of(&fmt, STR("oxXu"), &flag)) {
                value = va_arg(vlist, unsigned long long);
            } else {
                return -EINVAL;
            }
            goto write_int;
        }

        if (consume(&fmt, STR("l")) ||
            (sizeof(size_t) == sizeof(long) && consume(&fmt, STR("z")))) {
            if (consume(&fmt, STR("d")) || consume(&fmt, STR("i"))) {
                value = va_arg(vlist, long);
                fm.is_signed = true;
            } else if (consume_one_of(&fmt, STR("oxXu"), &flag)) {
                value = va_arg(vlist, unsigned long);
            } else {
                return -EINVAL;
            }
            goto write_int;
        }

        if (consume(&fmt, STR("d")) || consume(&fmt, STR("i"))) {
            value = va_arg(vlist, i32);
            fm.is_signed = true;
        } else if (consume_one_of(&fmt, STR("oxXu"), &flag)) {
            value = va_arg(vlist, u32);
        } else {
            return -EINVAL;
        }

    write_int:
        if (flag != 0) {
            fm.base = base_from_specifier(flag);
            fm.uppercase = is_uppercase_specifier(flag);
        }

        write_integer(&fb_state, &fm, value);
    }

    if (fb_state.capacity) {
        size_t last_char;

        last_char = MIN(fb_state.bytes_written, fb_state.capacity - 1);
        fb_state.buffer[last_char] = '\0';
    }

    return fb_state.bytes_written;
}
