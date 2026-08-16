#pragma once

#include <common/error.h>
#include <common/types.h>
#include <common/string.h>

#include <smbios_structures.h>

bool smbios_available(void);

enum smbios_id_type : u8 {
    SMBIOS_ID_NONE = 0,

    // Saved fields from smbios_bios_information
    SMBIOS_ID_BIOS_VENDOR,
    SMBIOS_ID_BIOS_VERSION,
    SMBIOS_ID_BIOS_RELEASE_DATE,

    // Saved fields from smbios_system_information
    SMBIOS_ID_SYSTEM_MANUFACTURER,
    SMBIOS_ID_SYSTEM_NAME,
    SMBIOS_ID_SYSTEM_VERSION,
    SMBIOS_ID_SYSTEM_SERIAL_NUMBER,
    SMBIOS_ID_SYSTEM_SKU_NUMBER,
    SMBIOS_ID_SYSTEM_FAMILY,

    // Saved fields from smbios_board_information
    SMBIOS_ID_BOARD_MANUFACTURER,
    SMBIOS_ID_BOARD_PRODUCT,
    SMBIOS_ID_BOARD_VERSION,
    SMBIOS_ID_BOARD_SERIAL_NUMBER,
    SMBIOS_ID_BOARD_ASSET_TAG,

    // Saved fields from smbios_chassis_information
    SMBIOS_ID_CHASSIS_MANUFACTURER,
    SMBIOS_ID_CHASSIS_TYPE,
    SMBIOS_ID_CHASSIS_VERSION,
    SMBIOS_ID_CHASSIS_SERIAL_NUMBER,
    SMBIOS_ID_CHASSIS_ASSET_TAG,

    SMBIOS_NUM_IDS,
};

enum smbios_entry_type : u8 {
    SMBIOS_ENTRY_TYPE_STRING,
    SMBIOS_ENTRY_TYPE_BYTE,
};

struct smbios_match_entry {
    enum smbios_id_type id : 5;
    enum smbios_entry_type type : 2;
    bool fuzzy : 1;

    union {
        const char *str;
        u8 byte;
    };
};

typedef struct smbios_match {
    /*
     * A name the client code may use to identify this entry or for pretty
     * printing. Not used by the matcher.
     */
    const char *name;

    /*
     * The callback that is invoked in case a match happens (aka all provided
     * entries match against the current system).
     */
    void (*on_match)(const struct smbios_match*);

    /*
     * An array of possible match entries to check against the current system.
     * All provided entries must match for the comparison to succeed, so this is
     * treated as an AND operation.
     */
    struct smbios_match_entry matches[4];

    /*
     * Private data not used by the matcher.
     */
    void *priv;
} smbios_match;

// Matches a string value exactly
#define SMBIOS_MATCH(type_id, value) {                           \
    .type = SMBIOS_ENTRY_TYPE_STRING, .id = SMBIOS_ID_##type_id, \
    .str = (value)                                               \
}

// Matches a string value if it contains this substring anywhere
#define SMBIOS_FUZZY_MATCH(type_id, value) {                     \
    .type = SMBIOS_ENTRY_TYPE_STRING, .id = SMBIOS_ID_##type_id, \
    .fuzzy = true,.str = (value)                                 \
}

// Matches a byte value (e.g. CHASSIS_TYPE)
#define SMBIOS_MATCH_BYTE(type_id, value) {                    \
    .type = SMBIOS_ENTRY_TYPE_BYTE, .id = SMBIOS_ID_##type_id, \
    .byte = (value)                                            \
}

/*
 * Find all entries in the array that match the current system. ->match_cb()
 * is invoked for every matching entry. Returns the number of entries that have
 * matched successfully.
 */
size_t smbios_match_system(const smbios_match *match_array);

/*
 * Returns the first match against the current system from the provided array
 * that happens to succeed.
 */
const smbios_match *smbios_first_match(const smbios_match *match_array);

struct smbios_id {
    union {
        const char *str;
        u8 byte;
    };

    enum smbios_entry_type type;
};

/*
 * Get a saved SMBIOS 'id' entry. 'out_id' may be a NULL pointer if the client
 * code simply wishes to check if a field exists without actually reading it.
 *
 * Returns true on success, false if the entry of this 'id' is absent or empty.
 */
bool smbios_get_id(enum smbios_id_type id, struct smbios_id *out_id);

static inline bool smbios_has_id(enum smbios_id_type id)
{
    return smbios_get_id(id, NULL);
}

static inline bool bios_version_check(const char *prefix)
{
    struct smbios_id bios_version;

    if (!smbios_get_id(SMBIOS_ID_BIOS_VERSION, &bios_version))
        return false;

    return strstr(bios_version.str, prefix) == bios_version.str;
}

typedef error_t (*smbios_callback)
    (const struct smbios_structure_hdr *hdr, void *user);

error_t smbios_for_each(smbios_callback cb, void *user);

/*
 * Get a string pointer from an SMBIOS "STRING" value field. 'value' must be a
 * pointer to a field inside 'hdr' for bounds checking. NULL is returned in case
 * the string is out-of-bounds, doesn't exist, or is simply empty.
 */
const char *smbios_get_string(
    const struct smbios_structure_hdr *hdr,
    const struct smbios_string_value *value
);

// Check if an SMBIOS entry is large enough to include 'field'
#define SMBIOS_FIELD_EXISTS(ptr, field) \
    ((ptr)->hdr.size >= (offsetof(typeof(*ptr), field) + sizeof((ptr)->field)))
