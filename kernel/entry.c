#define MSG_FMT(msg) "entry: " msg

#include <common/helpers.h>
#include <common/types.h>
#include <common/string_container.h>

#include <boot/boot.h>
#include <boot/ultra_protocol.h>

#include <initcall.h>
#include <log.h>
#include <bug.h>
#include <boot/alloc.h>
#include <param.h>

#include <private/unwind.h>
#include <private/param.h>
#include <private/arch/init.h>

struct boot_context g_boot_ctx;
ptr_t g_direct_map_base;

#define UATTR_EXTRACT(ctx_field, hdr) do {                     \
    WARN_ON(ctx_field != NULL);                                \
    ctx_field = container_of(hdr, typeof(*ctx_field), header); \
} while (0)

static void boot_context_init(struct ultra_boot_context *ctx)
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

static const char *platform_type_to_string(u32 type)
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

void entry(struct ultra_boot_context *ctx)
{
    struct ultra_platform_info_attribute *pi;
    error_t ret;

    print(
        "Starting ultra kernel v0.0.1 on %s (@%s, built on %s %s)\n",
        ULTRA_ARCH_EXECUTION_MODE_STRING, ULTRA_GIT_SHA, __DATE__, __TIME__
    );

    arch_init_early();
    boot_context_init(ctx);

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

    ret = unwind_init();
    if (is_error(ret))
        pr_warn("unwind_init() error %d, stack traces won't available\n", ret);

    boot_alloc_init();

    for (;;);
}
