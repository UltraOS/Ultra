#pragma once

#define DO_CONCAT(x, y) x##y
#define CONCAT(x, y) DO_CONCAT(x, y)
#define UNIQUE(x) CONCAT(x, __COUNTER__)

#define DO_TO_STR(x) #x
#define TO_STR(x) DO_TO_STR(x)

#define TAKE_SECOND_ARG(unused, x, ...) x

#ifdef __cplusplus
#define STATIC_ASSERT static_assert
#else
#define STATIC_ASSERT _Static_assert
#endif

#define ARE_SAME_TYPE(x, y) __builtin_types_compatible_p(typeof(x), typeof(y))
#define IS_CONSTEXPR(expr) __builtin_constant_p((expr))
#define CHOOSE_EXPR(cond, if_true, if_false) \
    __builtin_choose_expr((cond), (if_true), (if_false))
#define IS_POWER_OF_TWO(x) (__builtin_popcountll(x) == 1)

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

#define EMBED_STATIC_ASSERT(expr, msg) \
    sizeof(struct {STATIC_ASSERT((expr), msg);})

#define BUILD_BUG_ON_EMBED_WITH_MSG(expr) EMBED_STATIC_ASSERT(!(expr), msg)
#define BUILD_BUG_ON_EMBED(expr) \
    BUILD_BUG_ON_EMBED_WITH_MSG(expr, "BUILD BUG: " #expr " evaluated to true")

#define BUILD_BUG_ON_WITH_MSG(expr, msg) STATIC_ASSERT(!(expr), msg);
#define BUILD_BUG_ON(expr) \
    BUILD_BUG_ON_WITH_MSG(expr, "BUILD BUG: " #expr " evaluated to true")

#define EXPECT_SIZEOF(type, size)                                 \
    BUILD_BUG_ON_WITH_MSG(                                        \
        sizeof(type) != (size), "BUILD BUG: Unexpected type size" \
    )

#define STATIC_ASSERT_IF_CONSTEXPR(expr, msg) \
    CHOOSE_EXPR(IS_CONSTEXPR(expr), EMBED_STATIC_ASSERT((expr), msg), 0)

#define CEILING_DIVIDE(x, y) (!!(x) + (((x) - !!(x)) / (y)))

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define offset_of(var, member) __builtin_offsetof(typeof(var), member)
#define offset_of_after(var, member) \
    (offset_of(var, member) + sizeof(((typeof(var)*)0)->member))

#define sizeof_after(var, member) (sizeof(var) - offset_of_after(var, member))

#define PTR_ADD(ptr, count) ((typeof(ptr))(((char*)(ptr)) + count))

/*
 * Add an offset to a pointer that puts it outside the object bounds without
 * invoking UB.
 *
 * Compilers are allowed to assume and optimize based on the assumption that
 * pointer arithmetic never wraps around since doing pointer arithmetic outside
 * of object bounds is UB according to the C standard.
 *
 * Some extremely rare cases still require such arithmetic to work correctly
 * though. For example, pre-cpu variables, especially dynamically allocated
 * objects that may end up all over the address space and thus require wrapping
 * arithmetic to reach from the original per_cpu_offset.
 */
#define PTR_ADD_HIDE_UB(ptr, count) ({                   \
    unsigned long laundered_ptr;                         \
                                                         \
    asm volatile("" : "=r" (laundered_ptr) : "0" (ptr)); \
    (typeof(ptr))(laundered_ptr + count);                \
})

#define IS_SIGNED_TYPE(x) (((typeof(x))-1) < ((typeof(x))1))
#define IS_UNSIGNED_TYPE(x) (!IS_SIGNED_TYPE(x))

#ifdef __has_builtin
    #define HAS_BUILTIN(x) __has_builtin(x)
#else
    #define HAS_BUILTIN(x) 0
#endif
