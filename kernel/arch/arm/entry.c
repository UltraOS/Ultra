#include <boot/ultra_protocol.h>
#include <boot/entry.h>
#include <common/helpers.h>

void arm_entry(struct ultra_boot_context *ctx, uint32_t magic)
{
    if (magic != ULTRA_MAGIC)
        for (;;);

    UNUSED(ctx);
    entry();
}
