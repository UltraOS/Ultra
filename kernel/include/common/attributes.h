#pragma once

#ifdef __cplusplus
#define NORETURN [[noreturn]]
#else
#define NORETURN _Noreturn
#endif

#ifdef __GNUC__

#define PACKED __attribute__((packed))

#ifdef __clang__
#define PRINTF_DECL(fmt_idx, args_idx) \
    __attribute__((format(printf, fmt_idx, args_idx)))
#else
#define PRINTF_DECL(fmt_idx, args_idx) \
    __attribute__((format(gnu_printf, fmt_idx, args_idx)))
#endif

#define ALWAYS_INLINE inline __attribute__((always_inline))

#define ERROR_EMITTER(msg) __attribute__((__error__(msg)))

#define ALIAS_OF(func) __attribute__((alias(#func)))
#define SECTION(sec) __attribute__((section(#sec)))
#define USED __attribute__((used))
#define WEAK __attribute__((weak))
#define UNUSED_DECL __attribute__((unused))

#elif defined(_MSC_VER) // Support for running tests on Windows

#define PACKED
#define ALWAYS_INLINE
#define PRINTF_DECL(fmt_idx, args_idx)
#define ERROR_EMITTER(msg)
#define ALIAS_OF(func)
#define SECTION(sec)
#define WEAK
#define UNUSED_DECL

#else
#error Unknown/unsupported compiler
#endif
