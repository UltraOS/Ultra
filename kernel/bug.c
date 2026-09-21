#include <common/attributes.h>
#include <common/helpers.h>
#include <common/types.h>

#include <arch/registers.h>

#include <bug.h>
#include <linker.h>
#include <log.h>
#include <panic.h>
#include <symbols.h>

#include <private/arch/bug.h>
#include <private/bug.h>

static void finish_warn_report(struct registers *regs)
{
    dump_stack(LOG_LEVEL_WARN, regs);
}

#ifdef ARCH_BUG_TRAP_INSTRUCTION

/*
 * The structure emitted by BUG_TRAP(), which describes one specific
 * BUG()/WARN() caller. The pc/file references are self-relative, see
 * SELF_RELATIVE_TARGET().
 */
struct bug_entry {
    i32 pc_disp;
    i32 file_disp;

    u16 line;
    u16 flags;
};

EXPECT_SIZEOF(struct bug_entry, 12);
BUILD_BUG_ON(offsetof(struct bug_entry, pc_disp) != 0);
BUILD_BUG_ON(offsetof(struct bug_entry, file_disp) != 4);
BUILD_BUG_ON(offsetof(struct bug_entry, line) != 8);
BUILD_BUG_ON(offsetof(struct bug_entry, flags) != 10);

extern struct bug_entry
SECTION_ARRAY_BEGIN(BUG_TABLE_SECTION)[],
SECTION_ARRAY_END(BUG_TABLE_SECTION)[];

static struct bug_entry *find_bug_entry(reg_t pc)
{
    ssize_t i;
    struct bug_entry *entry;

    for (i = 0; i < SECTION_ARRAY_SIZE(BUG_TABLE_SECTION); i++) {
        entry = &SECTION_ARRAY_BEGIN(BUG_TABLE_SECTION)[i];

        if (SELF_RELATIVE_TARGET(entry->pc_disp) == pc)
            return entry;
    }

    return nullptr;
}

static bool resume_after_trap(struct registers *regs, reg_t pc)
{
    registers_set_pc(regs, pc + ARCH_BUG_TRAP_LENGTH);
    return true;
}

bool bug_handle_trap(struct registers *regs)
{
    reg_t pc;
    const char *file;
    struct bug_entry *entry;

    pc = registers_get_pc(regs);
    if (!address_is_kernel_code(pc))
        return false;

    entry = find_bug_entry(pc);
    if (entry == nullptr)
        return false;

    file = (const char*)SELF_RELATIVE_TARGET(entry->file_disp);

    if (!(entry->flags & BUG_FLAG_WARNING))
        panic("BUG at %s:%u in %pSM", file, entry->line, &pc);

    pr_warn("WARNING at %s:%u in %pSM\n", file, entry->line, &pc);
    finish_warn_report(regs);
    return resume_after_trap(regs, pc);
}

#else

NEVER_INLINE
void bug_report(const char *file, u32 line)
{
    ptr_t pc;

    pc = RETURN_ADDRESS();
    panic("BUG at %s:%u in %pRSM", file, line, &pc);
}

NEVER_INLINE
void warn_report(const char *file, u32 line)
{
    ptr_t pc;

    pc = RETURN_ADDRESS();
    pr_warn("WARNING at %s:%u in %pRSM\n", file, line, &pc);
    finish_warn_report(nullptr);
}

#endif
