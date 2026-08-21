#pragma once

#include <arch/private/idt.h>

/*
 * [0x00, 0x1F] CPU exceptions
 * [0x20, 0xEF] dynamically allocated device vectors
 * [0xF0, 0xFF] fixed vectors, wired directly to their handlers:
 *     [0xF0, 0xFC] reserved for IPIs, named as SMP work consumes them
 *     [0xFD, 0xFF] timer, error, spurious
 */
#define VECTOR_DYNAMIC_FIRST NUM_X86_EXCEPTIONS
#define VECTOR_DYNAMIC_LAST 0xEF
#define NUM_DYNAMIC_VECTORS (VECTOR_DYNAMIC_LAST - VECTOR_DYNAMIC_FIRST + 1)

#define VECTOR_FIXED_FIRST 0xF0
#define NUM_FIXED_VECTORS (NUM_IDT_ENTRIES - VECTOR_FIXED_FIRST)

#define VECTOR_IPI_FIRST 0xF0
#define VECTOR_IPI_LAST 0xFC

#define VECTOR_APIC_TIMER 0xFD
#define VECTOR_APIC_ERROR 0xFE
#define VECTOR_APIC_SPURIOUS 0xFF
