#include <common/attributes.h>
#include <symbols.h>

#include <private/arch/abortable_instructions.h>

extern const struct abortable_instruction
SECTION_ARRAY_BEGIN(ABORTABLE_INSTRUCTIONS_SECTION)[],
SECTION_ARRAY_END(ABORTABLE_INSTRUCTIONS_SECTION)[];

static const struct abortable_instruction *find_abortable_instruction(reg_t ip)
{
    ssize_t i;
    const struct abortable_instruction *ai;

    for (i = 0; i < SECTION_ARRAY_SIZE(ABORTABLE_INSTRUCTIONS_SECTION); i++) {
        ai = &SECTION_ARRAY_BEGIN(ABORTABLE_INSTRUCTIONS_SECTION)[i];

        if (ai->try_pc == ip)
            return ai;
    }

    return NULL;
}

bool handle_abortable_instruction(struct registers *regs)
{
    reg_t pc;
    const struct abortable_instruction *ai;

    pc = registers_get_pc(regs);
    if (!address_is_kernel_code(pc))
        return false;

    ai = find_abortable_instruction(pc);
    if (ai == NULL)
        return false;

    if ((ai->flags & ABORTABLE_INSTRUCTION_HAS_ARCH_HANDLER) &&
        arch_handle_abortable_instruction(regs, ai))
        return true;

    if (ai->flags & ABORTABLE_INSTRUCTION_ENOSYS_ON_ERROR)
        registers_set_return_value(regs, ENOSYS);
    else if (ai->flags & ABORTABLE_INSTRUCTION_EFAULT_ON_ERROR)
        registers_set_return_value(regs, EFAULT);

    registers_set_pc(regs, ai->abort_pc);
    return true;
}

WEAK bool arch_handle_abortable_instruction(
    struct registers *regs, const struct abortable_instruction *ai
)
{
    UNREFERENCED_PARAMETER(ai);
    UNREFERENCED_PARAMETER(regs);

    return false;
}
