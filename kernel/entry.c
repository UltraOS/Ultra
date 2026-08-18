#define MSG_FMT(msg) "entry: " msg

#include <common/helpers.h>
#include <common/types.h>
#include <common/string_container.h>

#include <boot/boot.h>
#include <boot/ultra_protocol.h>

#include <acpi.h>
#include <free_after_init.h>
#include <log.h>
#include <bug.h>
#include <boot/alloc.h>
#include <param.h>
#include <config.h>
#include <init_level.h>

#include <private/unwind.h>
#include <private/param.h>
#include <private/per_cpu.h>
#include <private/smbios.h>
#include <private/memory.h>

struct boot_context g_boot_ctx;
ptr_t g_direct_map_base;

static struct ultra_boot_context* INIT_DATA s_loader_ctx;

#define UATTR_EXTRACT(ctx_field, hdr) do {                     \
    WARN_ON(ctx_field != NULL);                                \
    ctx_field = container_of(hdr, typeof(*ctx_field), header); \
} while (0)

static INIT_CODE void boot_context_init(struct ultra_boot_context *ctx)
{
    struct ultra_attribute_header *hdr = ctx->attributes;
    size_t i;

    for (i = 0; i < ctx->attribute_count; i++, hdr = ULTRA_NEXT_ATTRIBUTE(hdr)) {
        switch (hdr->type) {
        case ULTRA_ATTRIBUTE_PLATFORM_INFO:
            UATTR_EXTRACT(g_boot_ctx.platform_info, hdr);
            break;

        case ULTRA_ATTRIBUTE_KERNEL_INFO:
            UATTR_EXTRACT(g_boot_ctx.kernel_info, hdr);
            break;

        case ULTRA_ATTRIBUTE_MEMORY_MAP:
            UATTR_EXTRACT(g_boot_ctx.memory_map, hdr);
            break;

        case ULTRA_ATTRIBUTE_COMMAND_LINE: {
            struct ultra_command_line_attribute *cmdline;

            cmdline = container_of(hdr, typeof(*cmdline), header);
            g_boot_ctx.cmdline = STR_RUNTIME(cmdline->text);
            break;
        }

        case ULTRA_ATTRIBUTE_FRAMEBUFFER_INFO:
            UATTR_EXTRACT(g_boot_ctx.fb, hdr);
            break;

        case ULTRA_ATTRIBUTE_MODULE_INFO:
            if (g_boot_ctx.num_modules == 0)
                UATTR_EXTRACT(g_boot_ctx.modules, hdr);
            g_boot_ctx.num_modules++;
            break;

        default:
            pr_warn("Unhandled ultra attribute 0x%08X\n", hdr->type);
            break;
        }
    }

    // Verify that all mandatory attributes are provided
    BUG_ON(
        g_boot_ctx.kernel_info == NULL ||
        g_boot_ctx.platform_info == NULL ||
        g_boot_ctx.memory_map == NULL
    );
}

static INIT_CODE const char *platform_type_to_string(u32 type)
{
    switch (type) {
    case ULTRA_PLATFORM_BIOS:
        return "BIOS";
    case ULTRA_PLATFORM_UEFI:
        return "UEFI";
    default:
        return "<unknown platform>";
    }
}

static error_t INIT_CODE boot_info_init(void)
{
    struct ultra_platform_info_attribute *pi;

    boot_context_init(s_loader_ctx);

    pi = g_boot_ctx.platform_info;
    g_direct_map_base = pi->higher_half_base;

    cmdline_parse(
        g_boot_ctx.cmdline, SECTION_ARRAY_ARGS(EARLY_PARAMETERS_SECTION), NULL
    );

    pr_info(
        "booted via %s (by %s)\n", platform_type_to_string(pi->platform_type),
        pi->loader_name
    );

    pr_info(
        "direct map set at 0x%016zX (%d pt levels)\n",
        g_direct_map_base, pi->page_table_depth
    );

    print(
        "Kernel command line: \"%s\"\n",
        str_empty(g_boot_ctx.cmdline) ? "<empty>" : g_boot_ctx.cmdline.text
    );

    return EOK;
}
INIT_CALL_AT(BOOT_INFO_AVAILABLE, boot_info_init);

void INIT_CODE entry(struct ultra_boot_context *ctx)
{
    print(
        "Starting ultra kernel v0.0.1 on %s (@%s, built on %s %s)\n",
        ULTRA_ARCH, ULTRA_GIT_SHA, __DATE__, __TIME__
    );

    s_loader_ctx = ctx;

    init_level_raise(INIT_LEVEL_PRE_BOOT);
    init_level_raise(INIT_LEVEL_BOOT_INFO_AVAILABLE);
    init_level_raise(INIT_LEVEL_BOOT_ALLOC_AVAILABLE);

    kernel_address_space_setup();
    init_level_raise(INIT_LEVEL_KERNEL_ADDRESS_SPACE_AVAILABLE);

    early_io_map_init();
    init_level_raise(INIT_LEVEL_EARLY_IO_AVAILABLE);

    smbios_setup();
    acpi_setup_tables();
    init_level_raise(INIT_LEVEL_PLATFORM_INFO_AVAILABLE);

    per_cpu_setup();
    init_level_raise(INIT_LEVEL_PER_CPU_AVAILABLE);

    kernel_memory_map_setup();
    buddy_setup();
    init_level_raise(INIT_LEVEL_BUDDY_AVAILABLE);

    kernel_heap_init();
    init_level_raise(INIT_LEVEL_HEAP_AVAILABLE);

    valloc_setup();
    init_level_raise(INIT_LEVEL_VALLOC_AVAILABLE);

    for (;;);
}
