#pragma once

#define PACKED [[gnu::packed]]
#define NOINLINE __attribute__((noinline))
#define ALWAYS_INLINE __attribute__((always_inline))

#define USED __attribute__((used))

#define SET_BIT(x) (1ul << (x))

#define IS_BIT_SET(mask, x) ((mask)&SET_BIT(x))

#define TO_STRING_HELPER(x) #x
#define TO_STRING(x) TO_STRING_HELPER(x)

#define cli() asm volatile("cli" :: \
                               : "memory")
#define sti() asm volatile("sti" :: \
                               : "memory")
#define hlt() asm volatile("hlt" :: \
                               : "memory")
#define hang() \
    for (;;) { \
        cli(); \
        hlt(); \
    }

#define pause __builtin_ia32_pause

#define KB (1024ul)
#define MB (1024ul * KB)
#define GB (1024ul * MB)

#define MAKE_NONCOPYABLE(class_name)        \
    class_name(const class_name&) = delete; \
    class_name& operator=(const class_name&) = delete;

#define MAKE_NONMOVABLE(class_name)    \
    class_name(class_name&&) = delete; \
    class_name& operator=(class_name&&) = delete;

#define MAKE_STATIC(class_name)  \
    MAKE_NONCOPYABLE(class_name) \
    MAKE_NONMOVABLE(class_name)  \
    class_name() = delete;

#define MAKE_SINGLETON(class_name, ...) \
    MAKE_NONCOPYABLE(class_name)        \
    MAKE_NONMOVABLE(class_name)         \
private:                                \
    class_name(__VA_ARGS__)
