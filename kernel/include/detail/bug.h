#pragma once

#include <common/helpers.h>
#include <linker.h>

#define BUG_FLAG_WARNING (1 << 0)
#define BUG_FLAG_ONCE (1 << 1)
#define BUG_FLAG_DONE (1 << 2)

#define BUG_TRAP(trap_flags)                                    \
    asm volatile(                                               \
        "990: " ARCH_BUG_TRAP_INSTRUCTION "\n"                  \
        ".pushsection .rodata.str1.1, \"aMS\", %progbits, 1\n"  \
        "991: .string \"" __FILE__ "\"\n"                       \
        ".popsection\n"                                         \
        ".pushsection ." TO_STR(BUG_TABLE_SECTION) ", \"aw\"\n" \
        ".balign 4\n"                                           \
        ".long 990b - .\n"                                      \
        ".long 991b - .\n"                                      \
        ".short " TO_STR(__LINE__) "\n"                         \
        ".short " TO_STR(trap_flags) "\n"                       \
        ".popsection\n"                                         \
    )
