#pragma once

#include <common/types.h>

/*
 * Make 'name' a macro for the compile-time constant 'value' in
 * <generated/asm_offsets.h>.
 */
#define ASM_CONSTANT(name, value)                               \
    asm volatile("\n.ascii \"->" #name " %c0\"" :: "i"(value))

// ASM_CONSTANT() of the offset of 'member' within 'type'
#define ASM_OFFSET(name, type, member) \
    ASM_CONSTANT(name, offsetof(type, member))

/*
 * To be defined by the arch in the translation unit that is passed to
 * setup_asm_offsets() (usually asm_offsets.c). This function is never
 * called, it's only used as a container for all ASM_{CONSTANT,OFFSET}()
 * constants an arch wants defined in <generated/asm_offsets.h>.
 */
void asm_offsets(void);
