#include <arch/private/unwind.h>

.section ".text", "ax", @progbits

.extern unwind_current_begin

.globl arch_unwind_current_begin
.type arch_unwind_current_begin, @function
arch_unwind_current_begin:
    str x0, [x0, DWARF_REG_X0 * 8]
    str x1, [x0, DWARF_REG_X1 * 8]
    str x2, [x0, DWARF_REG_X2 * 8]
    str x3, [x0, DWARF_REG_X3 * 8]
    str x4, [x0, DWARF_REG_X4 * 8]
    str x5, [x0, DWARF_REG_X5 * 8]
    str x6, [x0, DWARF_REG_X6 * 8]
    str x7, [x0, DWARF_REG_X7 * 8]
    str x8, [x0, DWARF_REG_X8 * 8]
    str x9, [x0, DWARF_REG_X9 * 8]
    str x10, [x0, DWARF_REG_X10 * 8]
    str x11, [x0, DWARF_REG_X11 * 8]
    str x12, [x0, DWARF_REG_X12 * 8]
    str x13, [x0, DWARF_REG_X13 * 8]
    str x14, [x0, DWARF_REG_X14 * 8]
    str x15, [x0, DWARF_REG_X15 * 8]
    str x16, [x0, DWARF_REG_X16 * 8]
    str x17, [x0, DWARF_REG_X17 * 8]
    str x18, [x0, DWARF_REG_X18 * 8]
    str x19, [x0, DWARF_REG_X19 * 8]
    str x20, [x0, DWARF_REG_X20 * 8]
    str x21, [x0, DWARF_REG_X21 * 8]
    str x22, [x0, DWARF_REG_X22 * 8]
    str x23, [x0, DWARF_REG_X23 * 8]
    str x24, [x0, DWARF_REG_X24 * 8]
    str x25, [x0, DWARF_REG_X25 * 8]
    str x26, [x0, DWARF_REG_X26 * 8]
    str x27, [x0, DWARF_REG_X27 * 8]
    str x28, [x0, DWARF_REG_X28 * 8]
    str x29, [x0, DWARF_REG_X29 * 8]

    str x30, [x0, DWARF_REG_X30 * 8]
    str x30, [x0, DWARF_REG_PC * 8]

    mov x2, sp
    str x2, [x0, DWARF_REG_SP * 8]
    ldr x2, [x0, DWARF_REG_X2 * 8]

    b unwind_current_begin
.size arch_unwind_current_begin, . - arch_unwind_current_begin
