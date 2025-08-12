#pragma once

#ifdef __cplusplus
#define NORETURN [[noreturn]]
#else
#define NORETURN _Noreturn
#endif

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
#define ALIGN(value) _Alignas(value)
#define USED __attribute__((used))
#define WEAK __attribute__((weak))
#define UNUSED_DECL __attribute__((unused))
#define FALLTHROUGH __attribute__((fallthrough))

#define SECTION_VAR(section, qualifiers, type) \
    USED SECTION(section) ALIGN(type) qualifiers type
