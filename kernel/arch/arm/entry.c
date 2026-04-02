#include <boot/ultra_protocol.h>
#include <boot/boot.h>
#include <common/helpers.h>

ULTRA_ENTRYPOINT(arm)
{
    if (magic != ULTRA_MAGIC)
        for (;;);

    entry(ctx);
}
