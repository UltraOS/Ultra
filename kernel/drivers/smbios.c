#define MSG_FMT(msg) "smbios: " msg

#include <private/smbios.h>
#include <smbios.h>

#include <boot/boot.h>
#include <io.h>
#include <free_after_init.h>

#include <common/byte_order.h>
#include <common/string.h>
#include <common/types.h>
#include <common/format.h>

static struct saved_smbios_id {
    struct smbios_id id;
    bool present;
} s_saved_ids[SMBIOS_NUM_IDS - 1];

#define CHECK_SIGNATURE(ptr, signature) \
    (memcmp((ptr), (signature), sizeof(signature) - 1) == 0)

static struct smbios_ctx {
    void *base;
    u8 major, minor, docrev;
    u32 size;
    i32 num_items;
    bool present;
} s_ctx;

bool smbios_available(void)
{
    return s_ctx.present;
}

bool smbios_get_id(enum smbios_id_type id, struct smbios_id *out_id)
{
    struct saved_smbios_id *saved_id;

    if (id == SMBIOS_ID_NONE || id > ARRAY_SIZE(s_saved_ids) ||
        !smbios_available())
        return false;

    // We always subtract 1 because id 0 is taken by SMBIOS_ID_NONE
    saved_id = &s_saved_ids[id - 1];
    if (!saved_id->present)
        return false;

    if (out_id != NULL)
        memcpy(out_id, &saved_id->id, sizeof(*out_id));

    return true;
}

static INIT_CODE bool checksum_ok(void *base, u32 size)
{
    u8 sum = 0;
    u8 *byte_ptr = base;

    while (size-- > 0)
        sum += *byte_ptr++;

    if (unlikely(sum != 0)) {
        pr_err("invalid checksum\n");
        return false;
    }

    return true;
}

static INIT_CODE void smbios3_setup(void *base)
{
    struct smbios3_entrypoint *entry = base;

    if (unlikely(entry->size < sizeof(struct smbios3_entrypoint) ||
                 entry->size > 0xFF)) {
        pr_err("invalid entrypoint size %d\n", entry->size);
        return;
    }

    if (!checksum_ok(entry, entry->size))
        return;

    s_ctx.base = phys_to_virt(le64_to_cpu(entry->table_address));
    s_ctx.num_items = -1;
    s_ctx.major = entry->major;
    s_ctx.minor = entry->minor;
    s_ctx.docrev = entry->docrev;
    s_ctx.size = le32_to_cpu(entry->max_structure_size);
    s_ctx.present = s_ctx.size != 0;
}

static INIT_CODE void dmi_setup(void *base)
{
    struct dmi_entrypoint *entry = base;

    if (!checksum_ok(entry, sizeof(struct dmi_entrypoint)))
        return;

    s_ctx.base = phys_to_virt(le32_to_cpu(entry->table_address));
    s_ctx.num_items = le16_to_cpu(entry->num_items);
    s_ctx.size = le16_to_cpu(entry->table_length);

    if (!s_ctx.major && !s_ctx.minor) {
        s_ctx.major = entry->revision >> 4;
        s_ctx.minor = entry->revision & 0x0F;
    }

    s_ctx.present = s_ctx.size && s_ctx.num_items;
}

static INIT_CODE void smbios2_setup(void *base)
{
    struct smbios2_entrypoint *entry = base;

    if (unlikely(entry->size < sizeof(struct smbios2_entrypoint) ||
                 entry->size > 0xFF)) {
        pr_err("invalid entrypoint size %d\n", entry->size);
        return;
    }

    if (!checksum_ok(entry, entry->size))
        return;

    s_ctx.major = entry->major;
    s_ctx.minor = entry->minor;
    dmi_setup(&entry->dmi_entryoint);
}

static INIT_CODE void smbios_save_string(
    enum smbios_id_type type, const struct smbios_structure_hdr *hdr,
    struct smbios_string_value *str_value
)
{
    const char *str;

    str = smbios_get_string(hdr, str_value);
    if (str == NULL)
        return;

    s_saved_ids[type - 1] = (struct saved_smbios_id) {
        .id = {
            .str = str,
            .type = SMBIOS_ENTRY_TYPE_STRING,
        },
        .present = true,
    };
}

static INIT_CODE void smbios_save_byte(enum smbios_id_type type, u8 byte)
{
    s_saved_ids[type - 1] = (struct saved_smbios_id) {
        .id = {
            .byte = byte,
            .type = SMBIOS_ENTRY_TYPE_BYTE,
        },
        .present = true,
    };
}

static INIT_CODE error_t smbios_parse(
    const struct smbios_structure_hdr *hdr, void *user
)
{
    UNREFERENCED_PARAMETER(user);

    switch (hdr->type) {
    case SMBIOS_STRUCTURE_TYPE_BIOS_INFORMATION: {
        struct smbios_bios_information *bios_info;

        bios_info = container_of(hdr, struct smbios_bios_information, hdr);
        smbios_save_string(SMBIOS_ID_BIOS_VENDOR, hdr, &bios_info->vendor);
        smbios_save_string(SMBIOS_ID_BIOS_VERSION, hdr, &bios_info->version);
        smbios_save_string(
            SMBIOS_ID_BIOS_RELEASE_DATE, hdr, &bios_info->release_date
        );
        break;
    }
    case SMBIOS_STRUCTURE_TYPE_SYSTEM_INFORMATION: {
        struct smbios_system_information *system_info;

        system_info = container_of(hdr, struct smbios_system_information, hdr);
        smbios_save_string(
            SMBIOS_ID_SYSTEM_MANUFACTURER, hdr, &system_info->manufacturer
        );
        smbios_save_string(
            SMBIOS_ID_SYSTEM_NAME, hdr, &system_info->product_name
        );
        smbios_save_string(
            SMBIOS_ID_SYSTEM_VERSION, hdr, &system_info->version
        );
        smbios_save_string(
            SMBIOS_ID_SYSTEM_SERIAL_NUMBER, hdr, &system_info->serial_number
        );
        smbios_save_string(
            SMBIOS_ID_SYSTEM_SKU_NUMBER, hdr, &system_info->sku_number
        );
        smbios_save_string(
            SMBIOS_ID_SYSTEM_FAMILY, hdr, &system_info->family
        );
        break;
    }
    case SMBIOS_STRUCTURE_TYPE_BOARD_INFORMATION: {
        struct smbios_board_information *board_info;

        board_info = container_of(hdr, struct smbios_board_information, hdr);
        smbios_save_string(
            SMBIOS_ID_BOARD_MANUFACTURER, hdr, &board_info->manufacturer
        );
        smbios_save_string(
            SMBIOS_ID_BOARD_PRODUCT, hdr, &board_info->product
        );
        smbios_save_string(
            SMBIOS_ID_BOARD_VERSION, hdr, &board_info->version
        );
        smbios_save_string(
            SMBIOS_ID_BOARD_SERIAL_NUMBER, hdr, &board_info->serial_number
        );
        smbios_save_string(
            SMBIOS_ID_BOARD_ASSET_TAG, hdr, &board_info->asset_tag
        );
        break;
    }
    case SMBIOS_STRUCTURE_TYPE_CHASSIS_INFORMATION: {
        struct smbios_chassis_information *chassis_info;

        chassis_info = container_of(hdr, struct smbios_chassis_information, hdr);
        smbios_save_string(
            SMBIOS_ID_CHASSIS_MANUFACTURER, hdr, &chassis_info->manufacturer
        );
        if (SMBIOS_FIELD_EXISTS(chassis_info, type))
            smbios_save_byte(SMBIOS_ID_CHASSIS_TYPE, chassis_info->type);
        smbios_save_string(
            SMBIOS_ID_CHASSIS_VERSION, hdr, &chassis_info->version
        );
        smbios_save_string(
            SMBIOS_ID_CHASSIS_SERIAL_NUMBER, hdr, &chassis_info->serial_number
        );
        smbios_save_string(
            SMBIOS_ID_CHASSIS_ASSET_TAG, hdr, &chassis_info->asset_tag
        );
        break;
    }
    default:
        break;
    }

    return EOK;
}

#define SMBIOS_ID_PRINT_ONE(fmt, ...) do {                    \
        this_write = scnprintf(                               \
            cursor, bytes_left, fmt __VA_OPT__(,) __VA_ARGS__ \
        );                                                    \
        cursor += this_write;                                 \
        bytes_left -= this_write;                             \
        non_empty = true;                                     \
    } while (0)

static INIT_CODE void smbios_setup_hardware_identity_string(void)
{
    char ident_str[256];
    char *cursor;
    int this_write, bytes_left = sizeof(ident_str);
    struct smbios_id id;
    bool non_empty = false;

    cursor = ident_str;

    if (smbios_get_id(SMBIOS_ID_SYSTEM_MANUFACTURER, &id))
        SMBIOS_ID_PRINT_ONE("%s", id.str);

    if (smbios_get_id(SMBIOS_ID_SYSTEM_NAME, &id)) {
        if (non_empty)
            SMBIOS_ID_PRINT_ONE(" ");
        SMBIOS_ID_PRINT_ONE("%s", id.str);
    }

    if (smbios_get_id(SMBIOS_ID_BOARD_PRODUCT, &id)) {
        if (non_empty)
            SMBIOS_ID_PRINT_ONE("/");
        SMBIOS_ID_PRINT_ONE("%s", id.str);
    }

    if (!smbios_has_id(SMBIOS_ID_BIOS_VENDOR) &&
        !smbios_has_id(SMBIOS_ID_BIOS_VERSION) &&
        !smbios_has_id(SMBIOS_ID_BIOS_RELEASE_DATE))
        // No BIOS information available :(
        goto done;

    if (!non_empty)
        /*
         * We don't know what hardware we're on, but at least we know the BIOS
         * version. Let's do what we can.
         */
        SMBIOS_ID_PRINT_ONE("Unknown Hardware");

    SMBIOS_ID_PRINT_ONE(", ");

    if (smbios_get_id(SMBIOS_ID_BIOS_VENDOR, &id)) {
        bool contains_platform;
        const char *platform =
            g_boot_ctx.platform_info->platform_type == ULTRA_PLATFORM_BIOS ?
            "BIOS" : "UEFI";

        contains_platform = str_contains(STR(id.str), STR(platform));
        SMBIOS_ID_PRINT_ONE("%s", id.str);
        if (!contains_platform)
            SMBIOS_ID_PRINT_ONE(" %s", platform);
    } else {
        SMBIOS_ID_PRINT_ONE("Unknown BIOS");
    }

    if (smbios_get_id(SMBIOS_ID_BIOS_VERSION, &id))
        SMBIOS_ID_PRINT_ONE(" %s", id.str);

    if (smbios_get_id(SMBIOS_ID_BIOS_RELEASE_DATE, &id))
        SMBIOS_ID_PRINT_ONE(" (%s)", id.str);

done:
    if (!non_empty)
        // Not enough data to make a meaningful identity string
        return;

    log_set_hardware_identity_string(ident_str);
    pr_info("%s\n", ident_str);
}

void INIT_CODE smbios_setup(void)
{
    phys_addr_t anchor = g_boot_ctx.platform_info->smbios_address;
    void *smbios_base;

    if (!anchor) {
        pr_info("not supported on this platform\n");
        return;
    }

    smbios_base = phys_to_virt(anchor);
    if (CHECK_SIGNATURE(smbios_base, SMBIOS3_SIGNATURE))
        smbios3_setup(smbios_base);
    else if (CHECK_SIGNATURE(smbios_base, SMBIOS2_SIGNATURE))
        smbios2_setup(smbios_base);
    else if (CHECK_SIGNATURE(smbios_base, DMI_SIGNATURE))
        dmi_setup(smbios_base);
    else
        pr_info("bad/unsupported signature\n");

    if (!smbios_available())
        return;

    pr_info("version %d.%d.%d\n", s_ctx.major, s_ctx.minor, s_ctx.docrev);

    if (is_error(smbios_for_each(smbios_parse, nullptr))) {
        pr_err("disabled due to parsing errors\n");
        return;
    }

    smbios_setup_hardware_identity_string();
}

error_t smbios_for_each(smbios_callback cb, void *user)
{
    i32 item_idx = 0;
    u32 bytes_left = s_ctx.size;
    void *ptr = s_ctx.base;
    error_t ret = ENODEV;

    if (!smbios_available())
        return ret;

    for (;;) {
        struct smbios_structure_hdr *hdr;
        u8 *probe_cursor;

        if (s_ctx.num_items > 0 && item_idx >= s_ctx.num_items)
            break;
        if (bytes_left == 0)
            break;

        hdr = ptr;
        if (unlikely(hdr->size > bytes_left || hdr->size < sizeof(*hdr))) {
            pr_err(
                "invalid entry[%d]: size %d (with %u bytes left)\n",
                item_idx, hdr->size, bytes_left
            );
            break;
        }
        if (hdr->type == SMBIOS_STRUCTURE_TYPE_END_OF_TABLE &&
            s_ctx.num_items < 0)
            break;

        bytes_left -= hdr->size;
        ptr += hdr->size;

        /*
         * Find the end of this entry & make sure it's not out-of-bounds in the
         * array of structures.
         */
        for (probe_cursor = ptr;; probe_cursor += 1, bytes_left -= 1) {
            // At least a \0 string + \0 to signify end-of-strings
            if (bytes_left < 2)
                goto out;

            if (probe_cursor[0] == '\0' && probe_cursor[1] == '\0') {
                // A valid entry that's properly terminated, we're done
                bytes_left -= 2;
                ptr = probe_cursor + 2;
                break;
            }
        }

        ret = cb(hdr, user);
        if (is_error(ret))
            return ret;

        item_idx++;
    }

out:
    if (s_ctx.num_items > 0 && item_idx < s_ctx.num_items) {
        pr_debug(
            "number of items truncated: %d -> %d\n",
            s_ctx.num_items, item_idx
        );
        s_ctx.num_items = item_idx;
    }

    if (bytes_left) {
        pr_debug(
            "table size truncated %d -> %d\n",
            s_ctx.size, s_ctx.size - bytes_left
        );
        s_ctx.size -= bytes_left;

        if (unlikely(bytes_left == 0)) {
            s_ctx.present = false;
            ret = EINVAL;
        }
    }

    return ret;
}

const char *smbios_get_string(
    const struct smbios_structure_hdr *hdr, struct smbios_string_value *value
)
{
    char *tmp_cursor, *cursor = (char*)hdr + hdr->size;
    u8 idx;
    size_t offset;

    offset = (ptr_t)value - (ptr_t)hdr;
    if (offset >= s_ctx.size)
        // This field is actually out-of-bounds of the structure
        return nullptr;

    if (value->idx_plus_one == 0)
        return nullptr;

    idx = value->idx_plus_one - 1;

    while (idx-- > 0 && cursor[0] != '\0')
        /*
         * strlen here is safe because of additional verifications done in
         * smbios_for_each().
         */
        cursor += strlen(cursor) + 1;

    // Verify that the string is not just spaces, if it is - treat it as null
    tmp_cursor = cursor;
    while (*tmp_cursor == ' ')
        tmp_cursor++;

    return tmp_cursor[0] != '\0' ? cursor : nullptr;
}

static bool smbios_match_string(
    const char *system, const char *user, bool fuzzy
)
{
    if (fuzzy)
        return strstr(system, user) != nullptr;

    return strcmp(system, user) == 0;
}

static bool smbios_match_has_entries(const smbios_match *match)
{
    // Sparse matches are not allowed so just check the first one
    return match->matches[0].id != SMBIOS_ID_NONE;
}

static bool smbios_check_match(const smbios_match *match)
{
    size_t i;
    const struct smbios_match_entry *me;
    struct smbios_id id;

    for (i = 0; i < ARRAY_SIZE(match->matches); i++) {
        me = &match->matches[i];

        if (me->id == SMBIOS_ID_NONE)
            break;
        if (!smbios_get_id(me->id, &id) || WARN_ON(me->type != id.type))
            return false;

        switch (id.type) {
        case SMBIOS_ENTRY_TYPE_STRING:
            if (smbios_match_string(id.str, me->str, me->fuzzy))
                continue;
            break;
        case SMBIOS_ENTRY_TYPE_BYTE:
            if (id.byte == me->byte)
                continue;
            break;
        default:
            break;
        }

        return false;
    }

    return true;
}

size_t smbios_match_system(const smbios_match *match)
{
    size_t num_matches = 0;

    if (!smbios_available())
        return num_matches;

    for (; smbios_match_has_entries(match); match++) {
        if (!smbios_check_match(match))
            continue;

        num_matches++;

        if (match->on_match)
            match->on_match(match);
    }

    return num_matches;
}

const smbios_match *smbios_first_match(const smbios_match *match)
{
    if (!smbios_available())
        return nullptr;

    for (; smbios_match_has_entries(match); match++) {
        if (!smbios_check_match(match))
            continue;

        return match;
    }

    return nullptr;
}
