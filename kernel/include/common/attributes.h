#pragma once

#define PACKED __attribute__((packed))

#define NORETURN _Noreturn

#ifdef __clang__
#define PRINTF_DECL(fmt_idx, args_idx) __attribute__((format(printf, fmt_idx, args_idx)))
#elif defined(__GNUC__)
#define PRINTF_DECL(fmt_idx, args_idx) __attribute__((format(gnu_printf, fmt_idx, args_idx)))
#else
#define PRINTF_DECL(fmt_idx, args_idx)
#endif

#define ALWAYS_INLINE inline __attribute__((always_inline))

#define ERROR_EMITTER(msg) __attribute__((__error__(msg)))

#define ALIAS_OF(func) __attribute__((alias(#func)))
#define SECTION(sec) __attribute__((section(#sec)))
#define USED __attribute__((used))
#define WEAK __attribute__((weak))
#define UNUSED_DECL __attribute__((unused))
