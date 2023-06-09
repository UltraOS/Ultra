#pragma once

#include "Macros.h"
#include "Traits.h"
#include "Types.h"

namespace kernel {

template <typename T>
enable_if_t<is_integral_v<T>, size_t> to_string(T number, char* string, size_t max_size, bool null_terminate = true)
{
    bool is_negative = false;
    size_t required_size = 0;

    if (number == 0) {
        if (max_size >= (null_terminate ? 2 : 1)) {
            string[0] = '0';
            if (null_terminate)
                string[1] = '\0';
            return null_terminate ? 2 : 1;
        }

        return 0;
    } else if (number < 0) {
        is_negative = true;
        number = -number;
        ++required_size;
    }

    T copy = number;

    while (copy) {
        copy /= 10;
        ++required_size;
    }

    if (required_size + !!null_terminate > max_size)
        return 0;

    T modulos = 0;
    for (size_t divisor = 0; divisor < required_size - !!is_negative; ++divisor) {
        modulos = number % 10;
        number /= 10;
        string[required_size - (divisor + 1)] = static_cast<char>(modulos + '0');
    }

    if (is_negative)
        string[0] = '-';

    if (null_terminate)
        string[required_size] = '\0';

    return required_size;
}

template <typename T>
enable_if_t<is_floating_point_v<T>, size_t>
to_string(T number, char* string, size_t max_size, bool null_terminate = true)
{
    number += 0.005; // to reduce the digit count

    i64 as_int = number;
    i64 remainder = static_cast<i64>(number * 100) % 100;

    size_t required_size = 0;
    i64 copy = as_int;

    while (copy) {
        copy /= 10;
        ++required_size;
    }

    size_t i = 0;
    while (as_int > 0 && max_size--) {
        string[required_size - i - 1] = (as_int % 10) + '0';
        as_int /= 10;
        ++i;
    }

    if (!i) {
        if (max_size < 1ul)
            return 0;
        else {
            string[i++] = '0';
            --max_size;
        }
    }

    if (max_size < 3ul + !!null_terminate)
        return 0;

    string[i++] = '.';
    string[i++] = (remainder / 10) + '0';
    string[i++] = (remainder % 10) + '0';

    if (null_terminate)
        string[i] = '\0';

    return required_size;
}

template <typename T>
enable_if_t<is_integral_v<T>, size_t> to_hex_string(T number, char* string, size_t max_size, bool null_terminate = true)
{
    constexpr auto required_length = sizeof(number) * 2 + 2; // '0x' + 2 chars per hex byte

    if (max_size < required_length + !!null_terminate)
        return 0;

    string[0] = '0';
    string[1] = 'x';

    if (null_terminate)
        string[required_length] = '\0';

    static constexpr auto hex_digits = "0123456789ABCDEF";

    size_t j = 0;
    for (size_t i = 0; j < sizeof(number) * 2 * 4; ++i) {
        string[required_length - i - 1] = hex_digits[(number >> j) & 0x0F];
        j += 4;
    }

    return required_length;
}

template <typename T>
enable_if_t<is_floating_point_v<T>, size_t>
to_hex_string(T number, char* string, size_t max_size, bool null_terminate = true)
{
    return to_hex_string(static_cast<i64>(number), string, max_size, null_terminate);
}

template <typename T>
enable_if_t<is_integral_v<T>, T> bytes_to_megabytes(T bytes)
{
    return bytes / MB;
}

template <typename T>
enable_if_t<is_arithmetic_v<T>, float> bytes_to_megabytes_precise(T bytes)
{
    return bytes / static_cast<float>(MB);
}

// clang-format off
template <typename T>
constexpr enable_if_t<is_integral_v<T>, T> bcd_to_binary(T bcd_value)
{
    if constexpr (sizeof(T) == 1)
            return  ((bcd_value & 0xF0) >> 4) * 10
                   + (bcd_value & 0x0F);
    else if constexpr (sizeof(T) == 2)
            return   ((bcd_value & 0xF000) >> 12) * 1000
                   + ((bcd_value & 0x0F00) >> 8 ) * 100
                   + ((bcd_value & 0x00F0) >> 4 ) * 10
                   +  (bcd_value & 0x000F);
    else if constexpr (sizeof(T) == 4)
            return   ((bcd_value & 0xF0000000) >> 28 ) * 10000000
                   + ((bcd_value & 0x0F000000) >> 24 ) * 1000000
                   + ((bcd_value & 0x00F00000) >> 20 ) * 100000
                   + ((bcd_value & 0x000F0000) >> 16 ) * 10000
                   + ((bcd_value & 0x0000F000) >> 12 ) * 1000
                   + ((bcd_value & 0x00000F00) >> 8 )  * 100
                   + ((bcd_value & 0x000000F0) >> 4 )  * 10
                   +  (bcd_value & 0x0000000F);
    else if constexpr (sizeof(T) == 8)
            return   ((bcd_value & 0xF000000000000000) >> 60 ) * 1000000000000000
                   + ((bcd_value & 0x0F00000000000000) >> 56 ) * 100000000000000
                   + ((bcd_value & 0x00F0000000000000) >> 52 ) * 10000000000000
                   + ((bcd_value & 0x000F000000000000) >> 48 ) * 1000000000000
                   + ((bcd_value & 0x0000F00000000000) >> 44 ) * 100000000000
                   + ((bcd_value & 0x00000F0000000000) >> 42 ) * 10000000000
                   + ((bcd_value & 0x000000F000000000) >> 40 ) * 1000000000
                   + ((bcd_value & 0x0000000F00000000) >> 36 ) * 100000000
                   + ((bcd_value & 0x00000000F0000000) >> 32 ) * 10000000
                   + ((bcd_value & 0x000000000F000000) >> 28 ) * 1000000
                   + ((bcd_value & 0x0000000000F00000) >> 24 ) * 100000
                   + ((bcd_value & 0x00000000000F0000) >> 20 ) * 10000
                   + ((bcd_value & 0x000000000000F000) >> 16 ) * 1000
                   + ((bcd_value & 0x0000000000000F00) >> 12 ) * 100
                   + ((bcd_value & 0x00000000000000F0) >> 8 )  * 10
                   +  (bcd_value & 0x000000000000000F);
    else
        return 0;
}
// clang-format on
}
