#pragma once

#include <boot/ultra_protocol.h>

#include <common/types.h>
#include <common/string_container.h>

#include <free_after_init.h>

#define ULTRA_ENTRYPOINT(prefix)                                         \
    void prefix##_entry(struct ultra_boot_context *ctx, uint32_t magic); \
    void INIT_CODE prefix##_entry(                                       \
        struct ultra_boot_context *ctx, uint32_t magic                   \
    )

struct boot_context {
    struct ultra_platform_info_attribute *platform_info;
    struct ultra_kernel_info_attribute *kernel_info;
    struct ultra_memory_map_attribute *memory_map;
    struct ultra_framebuffer_attribute *fb;

    struct ultra_module_info_attribute *modules;
    size_t num_modules;

    struct string cmdline;
};

static inline bool INIT_CODE ultra_mme_is_ram(
    struct ultra_memory_map_entry *mme
)
{
    switch (mme->type) {
    case ULTRA_MEMORY_TYPE_FREE:
    case ULTRA_MEMORY_TYPE_ACPI_RECLAIMABLE:
    case ULTRA_MEMORY_TYPE_LOADER_RECLAIMABLE:
    case ULTRA_MEMORY_TYPE_MODULE:
    case ULTRA_MEMORY_TYPE_KERNEL_STACK:
    case ULTRA_MEMORY_TYPE_KERNEL_BINARY:
        return true;
    default:
        return false;
    }
}

typedef bool (*memory_filter_cb_t)(
    struct ultra_memory_map_entry *mme
);

typedef void (*memory_map_range_cb_t)(
    phys_addr_t start, phys_addr_t end, u32 type, void *user
);

void for_each_memory_map_range(
    memory_map_range_cb_t, memory_filter_cb_t, void *user
);

typedef void (*memory_range_cb_t)(
    phys_addr_t start, phys_addr_t end, void *user
);

void for_each_ram_range(memory_range_cb_t, void *user);

extern struct boot_context g_boot_ctx;

void entry(struct ultra_boot_context *ctx);
