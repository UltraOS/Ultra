#pragma once

#include <common/types.h>
#include <common/error.h>

/*
 * Try to memcpy to or from an unverified kernel address.
 *
 * Returns EOK on success, or EFAULT if an error has occurred during the
 * operation.
 */
error_t try_memcpy(void *dst, const void *src, size_t n);
