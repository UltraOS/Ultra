#pragma once

#include <common/types.h>

// Use the host errno header to avoid collisions
#include <errno.h>

#define MAX_ERRNO 4095
#define EOK 0

// positive errno return type
typedef int error_t;

// negative errno return type
typedef int nerror_t;

#define is_error(ret) unlikely((ret) != EOK)
#define is_nerror(ret) unlikely((ret) < EOK)

typedef void *ptr_or_error_t;
typedef phys_addr_t phys_addr_or_error_t;

/*
 * These macros are used as a hint to the reader that a function may also return
 * an error code even though its return type is not {n}error_t.
 */
#define MAYBE_ERR(value) value
#define MAYBE_NERR(value) value

#define encode_error_ptr(value) ((void*)((ptr_t)(value)))
#define decode_error_ptr(value) ((error_t)((ptr_t)(value)))
#define error_ptr(ret) unlikely(((ptr_t)(ret)) <= MAX_ERRNO)

#define encode_error_phys_addr(value) ((phys_addr_t)(value))
#define decode_error_phys_addr(value) ((error_t)(value))
#define error_phys_addr(ret) unlikely(ret <= MAX_ERRNO)
