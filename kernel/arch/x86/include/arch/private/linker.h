#pragma once

#include <linker.h>

#define IDT_THUNKS_SECTION idt_thunks

/*
 * The real section must exist within .text because ld.lld produces a bogus
 * relocation otherwise:
 *     non-ABS relocation R_X86_64_PLT32 against symbol ''
 */
#define IDT_THUNKS_ASM_SECTION text.IDT_THUNKS_SECTION
