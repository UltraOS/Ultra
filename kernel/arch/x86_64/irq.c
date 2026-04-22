#include <log.h>

#include <arch/private/idt.h>
#include <arch/registers.h>

IRQ_HANDLER {
    pr_warn("Unexpected irq %u\n", regs->interrupt_idx);
}
