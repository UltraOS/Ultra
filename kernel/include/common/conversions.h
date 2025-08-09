#pragma once

#include <common/string_container.h>
#include <common/types.h>
#include <common/error.h>

#define STR_TO_N_DECL(bits)                                               \
    error_t str_to_i##bits##_with_base(                                   \
        struct string str, i##bits *res, unsigned int base                \
    );                                                                    \
    error_t str_to_u##bits##_with_base(                                   \
        struct string str, u##bits *res, unsigned int base                \
    );                                                                    \
                                                                          \
    static inline error_t str_to_i##bits(struct string str, i##bits*res)  \
    {                                                                     \
        return str_to_i##bits##_with_base(str, res, 0);                   \
    }                                                                     \
                                                                          \
    static inline error_t str_to_u##bits(struct string str, u##bits *res) \
    {                                                                     \
        return str_to_u##bits##_with_base(str, res, 0);                   \
    }

STR_TO_N_DECL(8)
STR_TO_N_DECL(16)
STR_TO_N_DECL(32)
STR_TO_N_DECL(64)

error_t str_to_bool(struct string, bool *res);
