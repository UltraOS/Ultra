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

/*
 * A hint to the reader that a function may also return an error code even
 * though its return type is not nerror_t.
 */
#define MAYBE_NERR(value) value
