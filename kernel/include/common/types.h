#pragma once

#include <common/helpers.h>

/*
 * Typed constants for headers shared with assembly and linker scripts.
 * LITERAL_OF_TYPE() only takes a number but also works in #if,
 * CONST_OF_TYPE() takes any expression.
 */
#ifdef __ASSEMBLER__
#define LITERAL_OF_TYPE(type, x) x
#define CONST_OF_TYPE(type, x) x
#else
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

// signed types
typedef int8_t i8;
typedef int16_t i16;
typedef int i32;
typedef signed long long i64;
typedef signed __int128 i128;

// unsigned types
typedef uint8_t u8;
typedef uint16_t u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef unsigned __int128 u128;

typedef size_t ptr_t;
typedef size_t reg_t;

typedef reg_t irq_state_t;

#if ULTRA_ARCH_PHYS_ADDR_WIDTH == 4
typedef u32 phys_addr_t;
#define LITERAL_SUFFIX_phys_addr_t LITERAL_SUFFIX_u32
#elif ULTRA_ARCH_PHYS_ADDR_WIDTH == 8
typedef u64 phys_addr_t;
#define LITERAL_SUFFIX_phys_addr_t LITERAL_SUFFIX_u64
#else
#error Unsupported ULTRA_ARCH_PHYS_ADDR_WIDTH
#endif

typedef ptr_t virt_addr_t;

#define LITERAL_SUFFIX_i32
#define LITERAL_SUFFIX_i64 ll
#define LITERAL_SUFFIX_u32 u
#define LITERAL_SUFFIX_u64 ull
#define LITERAL_SUFFIX_ptr_t ul
#define LITERAL_SUFFIX_reg_t ul
#define LITERAL_SUFFIX_virt_addr_t LITERAL_SUFFIX_ptr_t

#define LITERAL_OF_TYPE(type, x) CONCAT(x, LITERAL_SUFFIX_##type)
#define CONST_OF_TYPE(type, x) ((type)(x))

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

// __builtin_types_compatible_p() does not exist in C++
#ifndef __cplusplus
BUILD_BUG_ON(!ARE_SAME_TYPE(LITERAL_OF_TYPE(i32, 1), i32));
BUILD_BUG_ON(!ARE_SAME_TYPE(LITERAL_OF_TYPE(i64, 1), i64));
BUILD_BUG_ON(!ARE_SAME_TYPE(LITERAL_OF_TYPE(u32, 1), u32));
BUILD_BUG_ON(!ARE_SAME_TYPE(LITERAL_OF_TYPE(u64, 1), u64));
BUILD_BUG_ON(!ARE_SAME_TYPE(LITERAL_OF_TYPE(ptr_t, 1), ptr_t));
BUILD_BUG_ON(!ARE_SAME_TYPE(LITERAL_OF_TYPE(reg_t, 1), reg_t));
BUILD_BUG_ON(!ARE_SAME_TYPE(LITERAL_OF_TYPE(virt_addr_t, 1), virt_addr_t));
BUILD_BUG_ON(!ARE_SAME_TYPE(LITERAL_OF_TYPE(phys_addr_t, 1), phys_addr_t));
#endif
#endif
