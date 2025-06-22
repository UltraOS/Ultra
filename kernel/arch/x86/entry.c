#include <boot/ultra_protocol.h>
#include <boot/boot.h>

void x86_entry(struct ultra_boot_context *ctx, uint32_t magic)
{
    if (magic != ULTRA_MAGIC)
        for (;;);

    entry(ctx);
}
