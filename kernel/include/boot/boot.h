#pragma once

#include <boot/ultra_protocol.h>

#include <common/types.h>
#include <common/string_container.h>

struct boot_context {
    struct ultra_platform_info_attribute *platform_info;
    struct ultra_kernel_info_attribute *kernel_info;
    struct ultra_memory_map_attribute *memory_map;
    struct ultra_framebuffer_attribute *fb;

    struct ultra_module_info_attribute *modules;
    size_t num_modules;

    struct string cmdline;
};

extern struct boot_context g_boot_ctx;

void entry(struct ultra_boot_context *ctx);
