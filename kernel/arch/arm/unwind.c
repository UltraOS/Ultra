#include <private/arch/unwind.h>
#include <arch/private/unwind.h>

void arch_registers_to_dwarf_registers(
    struct registers *regs, ptr_t *dwarf_regs
)
{
    dwarf_regs[DWARF_REG_X0] = regs->x[0];
    dwarf_regs[DWARF_REG_X1] = regs->x[1];
    dwarf_regs[DWARF_REG_X2] = regs->x[2];
    dwarf_regs[DWARF_REG_X3] = regs->x[3];
    dwarf_regs[DWARF_REG_X4] = regs->x[4];
    dwarf_regs[DWARF_REG_X5] = regs->x[5];
    dwarf_regs[DWARF_REG_X6] = regs->x[6];
    dwarf_regs[DWARF_REG_X7] = regs->x[7];
    dwarf_regs[DWARF_REG_X8] = regs->x[8];
    dwarf_regs[DWARF_REG_X9] = regs->x[9];
    dwarf_regs[DWARF_REG_X10] = regs->x[10];
    dwarf_regs[DWARF_REG_X11] = regs->x[11];
    dwarf_regs[DWARF_REG_X12] = regs->x[12];
    dwarf_regs[DWARF_REG_X13] = regs->x[13];
    dwarf_regs[DWARF_REG_X14] = regs->x[14];
    dwarf_regs[DWARF_REG_X15] = regs->x[15];
    dwarf_regs[DWARF_REG_X16] = regs->x[16];
    dwarf_regs[DWARF_REG_X17] = regs->x[17];
    dwarf_regs[DWARF_REG_X18] = regs->x[18];
    dwarf_regs[DWARF_REG_X19] = regs->x[19];
    dwarf_regs[DWARF_REG_X20] = regs->x[20];
    dwarf_regs[DWARF_REG_X21] = regs->x[21];
    dwarf_regs[DWARF_REG_X22] = regs->x[22];
    dwarf_regs[DWARF_REG_X23] = regs->x[23];
    dwarf_regs[DWARF_REG_X24] = regs->x[24];
    dwarf_regs[DWARF_REG_X25] = regs->x[25];
    dwarf_regs[DWARF_REG_X26] = regs->x[26];
    dwarf_regs[DWARF_REG_X27] = regs->x[27];
    dwarf_regs[DWARF_REG_X28] = regs->x[28];
    dwarf_regs[DWARF_REG_X29] = regs->x[29];
    dwarf_regs[DWARF_REG_X30] = regs->x[30];

    dwarf_regs[DWARF_REG_PC] = regs->pc;
    dwarf_regs[DWARF_REG_SP] = regs->sp;
}
