#include <common/attributes.h>
#include <common/helpers.h>
#include <common/types.h>

#include <bug.h>
#include <log.h>
#include <panic.h>

static void finish_warn_report(struct registers *regs)
{
    dump_stack(LOG_LEVEL_WARN, regs);
}

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
