#pragma once

#include <common/types.h>
#include <common/error.h>
#include <common/attributes.h>

#include <memory/io.h>

#include <arch/io.h>

#ifndef ARCH_HAS_CUSTOM_PIO
    #define encode_error_pio_addr(ret) encode_error_ptr(ret)
    #define decode_error_pio_addr(ret) decode_error_ptr(ret)
    #define error_pio_addr(ret) error_ptr(ret)

    typedef pio_addr_t pio_addr_or_error_t;
#endif
