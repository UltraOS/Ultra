#pragma once

#define DO_CONCAT(x, y) x##y
#define CONCAT(x, y) DO_CONCAT(x, y)
#define UNIQUE(x) CONCAT(x, __COUNTER__)

#define DO_TO_STR(x) #x
#define TO_STR(x) DO_TO_STR(x)

#ifdef __cplusplus
#define STATIC_ASSERT static_assert
#else
#define STATIC_ASSERT _Static_assert
#endif

#ifdef __GNUC__

#define ARE_SAME_TYPE(x, y) __builtin_types_compatible_p(typeof(x), typeof(y))

#define DO_CONTAINER_OF(ptr, ptr_name, type, member) ({                    \
    char *ptr_name = (char*)(ptr);                                         \
    BUILD_BUG_ON(!ARE_SAME_TYPE(*(ptr), ((type*)sizeof(type))->member) &&  \
                 !ARE_SAME_TYPE(*(ptr), void));                            \
    ((type*)(ptr_name - offsetof(type, member))); })

#define container_of(ptr, type, member) \
    DO_CONTAINER_OF(ptr, UNIQUE(uptr), type, member)

#define likely(expr)   __builtin_expect(!!(expr), 1)
#define unlikely(expr) __builtin_expect(!!(expr), 0)

#define UNREFERENCED_PARAMETER(x) (void)(x)

#define BUILD_BUG_ON_WITH_MSG(expr, msg) STATIC_ASSERT(!(expr), msg)
#define BUILD_BUG_ON(expr) \
    BUILD_BUG_ON_WITH_MSG(expr, "BUILD BUG: " #expr " evaluated to true")

#elif defined(_MSC_VER)

#ifndef ULTRA_TEST
#error MSVC is only supported in test mode
#endif

#include <windows.h>
#define container_of(ptr, type, member) CONTAINING_RECORD(ptr, type, member)
// UNREFERENCED_PARAMETER also defined by windows.h

#define likely(expr) expr
#define unlikely(expr) expr

#define BUILD_BUG_ON_WITH_MSG(expr, msg)
#define BUILD_BUG_ON(expr)

#else
#error Unknown/unsupported compiler
#endif

#define CEILING_DIVIDE(x, y) (!!(x) + (((x) - !!(x)) / (y)))

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
