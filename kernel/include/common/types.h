#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <common/helpers.h>

// signed types
typedef int8_t i8;
typedef int16_t i16;
typedef int i32;
typedef signed long long i64;

// unsigned types
typedef uint8_t u8;
typedef uint16_t u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef size_t ptr_t;

#if ULTRA_ARCH_PHYS_ADDR_WIDTH == 4
typedef u32 phys_addr_t;
#else
typedef u64 phys_addr_t;
#endif

typedef ptr_t virt_addr_t;

#if !defined(ULTRA_TEST) || defined(_MSC_VER)
#if UINTPTR_MAX == 0xFFFFFFFF
typedef i32 ssize_t;
#else
typedef i64 ssize_t;
#endif
#else
#include <sys/types.h>
#endif

BUILD_BUG_ON(sizeof(i8) != 1);
BUILD_BUG_ON(sizeof(i16) != 2);
BUILD_BUG_ON(sizeof(i32) != 4);
BUILD_BUG_ON(sizeof(i64) != 8);

BUILD_BUG_ON(sizeof(u8) != 1);
BUILD_BUG_ON(sizeof(u16) != 2);
BUILD_BUG_ON(sizeof(u32) != 4);
BUILD_BUG_ON(sizeof(u64) != 8);

BUILD_BUG_ON(sizeof(bool) != 1);
