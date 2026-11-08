#include <common/attributes.h>
#include <common/error.h>
#include <symbols.h>

#include <private/arch/abortable_instructions.h>

DECLARE_TYPED_SECTION_MARKERS(
    const struct abortable_instruction, ABORTABLE_INSTRUCTIONS_SECTION
);

static const struct abortable_instruction *find_abortable_instruction(reg_t ip)
{
    const struct abortable_instruction *ai;

    for_each_section_array_item(ai, ABORTABLE_INSTRUCTIONS_SECTION) {
        if (SELF_RELATIVE_TARGET(ai->try_pc_disp) == ip)
            return ai;
    }

    return nullptr;
}

bool handle_abortable_instruction(struct registers *regs)
{
    reg_t pc;
    const struct abortable_instruction *ai;

    pc = registers_get_pc(regs);
    if (!address_is_kernel_code(pc))
        return false;

    ai = find_abortable_instruction(pc);
    if (ai == nullptr)
        return false;

    if ((ai->flags & ABORTABLE_INSTRUCTION_HAS_ARCH_HANDLER) &&
        arch_handle_abortable_instruction(regs, ai))
        return true;

    if (ai->flags & ABORTABLE_INSTRUCTION_ENOSYS_ON_ERROR)
        registers_set_return_value(regs, ENOSYS);
    else if (ai->flags & ABORTABLE_INSTRUCTION_EFAULT_ON_ERROR)
        registers_set_return_value(regs, EFAULT);

    registers_set_pc(regs, SELF_RELATIVE_TARGET(ai->abort_pc_disp));
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
