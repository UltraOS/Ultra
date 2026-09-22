#define MSG_FMT(msg) "flanterm: " msg

#include <init_level.h>
#include <console.h>

#include <memory/io.h>
#include <memory/alloc.h>
#include <boot/boot.h>

#include <flanterm.h>
#include <flanterm_backends/fb.h>

#define BACKGROUND_COLOR 0x101216
#define FOREGROUND_COLOR 0x8B949E

static u32 s_normal_colors[8] = {
    0x000000, 0xF78166, 0x56D364, 0xE3B341,
    0x6CA4F8, 0xDB61A2, 0x2B7489, 0xFFFFFF,
};

static u32 s_bright_colors[8] = {
    0x4D4D4D, 0xF78166, 0x56D364, 0xE3B341,
    0x6CA4F8, 0xDB61A2, 0x2B7489, 0xFFFFFF,
};

static io_window s_fb_mapping;
static struct flanterm_context *s_flanterm_ctx;

static void flanterm_console_write(
    struct console *con, const char *str, size_t count
)
{
    UNREFERENCED_PARAMETER(con);

    while (count--) {
        if (*str == '\n')
            flanterm_write(s_flanterm_ctx, "\r", 1);

        flanterm_write(s_flanterm_ctx, str++, 1);
    }

    flanterm_flush(s_flanterm_ctx);
}

static struct console s_flanterm_console = {
    .name = "flanterm",
    .flags = CONSOLE_FLAG_ANSI_COLOR | CONSOLE_FLAG_ANSI_NO_DIM,
    .write = flanterm_console_write,
};

static void *flanterm_alloc(size_t bytes)
{
    return alloc(bytes, ALLOC_GENERIC);
}

static void flanterm_free(void *ptr, size_t bytes)
{
    UNREFERENCED_PARAMETER(bytes);

    free(ptr);
}

static error_t INIT_CODE flanterm_console_register(void)
{
    error_t ret;
    struct ultra_framebuffer *fb;
    u8 r_shift, g_shift, b_shift;
    u32 background_color = BACKGROUND_COLOR;
    u32 foreground_color = FOREGROUND_COLOR;

    if (!g_boot_ctx.fb) {
        pr_notice("no framebuffer available\n");
        return EOK;
    }

    fb = &g_boot_ctx.fb->fb;

    switch (fb->format) {
    case ULTRA_FB_FORMAT_RGBX8888:
        r_shift = 24;
        g_shift = 16;
        b_shift = 8;
        break;
    case ULTRA_FB_FORMAT_XRGB8888:
        b_shift = 0;
        g_shift = 8;
        r_shift = 16;
        break;
    default:
        pr_warn(
            "unsupported framebuffer format %u (%u bpp)\n",
            fb->format, fb->bpp
        );
        return ENOSYS;
    }

    ret = io_window_map_wc(
        fb->physical_address, fb->pitch * fb->height, &s_fb_mapping
    );
    if (is_error(ret))
        return ret;

    s_flanterm_ctx = flanterm_fb_init(
        flanterm_alloc, flanterm_free,
        io_window_raw_ptr(&s_fb_mapping), fb->width, fb->height, fb->pitch,
        8, r_shift, 8, g_shift, 8, b_shift,
        nullptr,
        s_normal_colors, s_bright_colors,
        &background_color, &foreground_color,
        nullptr, nullptr,
        nullptr, 0, 0, 1,
        0, 0,
        0,
        0, false
    );

    if (unlikely(s_flanterm_ctx == nullptr)) {
        pr_warn("unable to initialize backend\n");
        ret = ENOMEM;
        goto out_unmap;
    }

    ret = register_console(&s_flanterm_console);
    if (is_error(ret)) {
        flanterm_deinit(s_flanterm_ctx, flanterm_free);
        goto out_unmap;
    }

    pr_info("%ux%u console\n", fb->width, fb->height);
    return ret;

out_unmap:
    io_window_unmap(&s_fb_mapping);
    return ret;
}
INIT_CALL_POST(VALLOC_AVAILABLE, flanterm_console_register);
