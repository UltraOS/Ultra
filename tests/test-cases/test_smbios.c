// Pull in the real implementation so we can poke at internal state.
#include <kernel-source/drivers/smbios.c>

#include <test_harness.h>

static void reset_smbios_ids(void)
{
    memset(s_saved_ids, 0, sizeof(s_saved_ids));
    s_ctx.present = true;
}

static void set_string_id(enum smbios_id_type id, const char *value)
{
    struct saved_smbios_id *saved = &s_saved_ids[id - 1];

    saved->id.str = value;
    saved->id.type = SMBIOS_ENTRY_TYPE_STRING;
    saved->present = true;
}

static void set_byte_id(enum smbios_id_type id, u8 value)
{
    struct saved_smbios_id *saved = &s_saved_ids[id - 1];

    saved->id.byte = value;
    saved->id.type = SMBIOS_ENTRY_TYPE_BYTE;
    saved->present = true;
}

static size_t s_match_callback_calls;

static void counting_match_cb(const struct smbios_match *match)
{
    UNREFERENCED_PARAMETER(match);
    s_match_callback_calls++;
}

TEST_CASE(smbios_match_exact_and_first)
{
    const smbios_match *first;

    reset_smbios_ids();
    set_string_id(SMBIOS_ID_SYSTEM_MANUFACTURER, "QEMU");
    set_string_id(SMBIOS_ID_BOARD_PRODUCT, "QEMU Virtual Machine");

    smbios_match table[] = {
        {
            .name = "qemu-strict",
            .on_match = counting_match_cb,
            .matches = {
                SMBIOS_MATCH(SYSTEM_MANUFACTURER, "QEMU"),
                SMBIOS_MATCH(BOARD_PRODUCT, "QEMU Virtual Machine"),
            },
        },
        {
            .name = "non-matching",
            .on_match = counting_match_cb,
            .matches = {
                SMBIOS_MATCH(SYSTEM_MANUFACTURER, "VMware"),
            },
        },
        {},
    };

    s_match_callback_calls = 0;

    ASSERT_EQ(smbios_match_system(table), (size_t)1);
    ASSERT_EQ(s_match_callback_calls, (size_t)1);

    first = smbios_first_match(table);
    ASSERT_NE(first, NULL);
    ASSERT_STR_EQ(first->name, "qemu-strict");
}

TEST_CASE(smbios_match_fuzzy_and_exact)
{
    const smbios_match *first;

    reset_smbios_ids();
    set_string_id(SMBIOS_ID_SYSTEM_SERIAL_NUMBER, "VMware-56 4d 8e ...");
    set_string_id(SMBIOS_ID_BIOS_VENDOR, "Bochs BIOS");

    smbios_match table[] = {
        {
            .name = "vmware",
            .matches = {
                SMBIOS_FUZZY_MATCH(SYSTEM_SERIAL_NUMBER, "VMware"),
            },
        },
        {
            .name = "bochs",
            .matches = {
                SMBIOS_FUZZY_MATCH(BIOS_VENDOR, "Bochs"),
            },
        },
        {
            .name = "no-match",
            .matches = {
                SMBIOS_FUZZY_MATCH(SYSTEM_MANUFACTURER, "NotPresent"),
            },
        },
        {},
    };

    ASSERT_EQ(smbios_match_system(table), (size_t)2);

    first = smbios_first_match(table);
    ASSERT_NE(first, NULL);
    ASSERT_STR_EQ(first->name, "vmware");
}

TEST_CASE(smbios_match_byte_and_mixed_entries)
{
    const smbios_match *first;

    reset_smbios_ids();
    set_string_id(SMBIOS_ID_CHASSIS_MANUFACTURER, "ACME");
    set_byte_id(SMBIOS_ID_CHASSIS_TYPE, 3);

    smbios_match table[] = {
        {
            .name = "chassis-ok",
            .matches = {
                SMBIOS_MATCH(CHASSIS_MANUFACTURER, "ACME"),
                SMBIOS_MATCH_BYTE(CHASSIS_TYPE, 3),
            },
        },
        {
            .name = "wrong-type",
            .matches = {
                SMBIOS_MATCH(CHASSIS_MANUFACTURER, "ACME"),
                SMBIOS_MATCH_BYTE(CHASSIS_TYPE, 2),
            },
        },
        {},
    };

    ASSERT_EQ(smbios_match_system(table), (size_t)1);

    first = smbios_first_match(table);
    ASSERT_NE(first, NULL);
    ASSERT_STR_EQ(first->name, "chassis-ok");
}

TEST_CASE(smbios_match_requires_all_entries)
{
    reset_smbios_ids();
    // Only manufacturer is present; name is deliberately missing
    set_string_id(SMBIOS_ID_SYSTEM_MANUFACTURER, "QEMU");

    smbios_match table[] = {
        {
            .name = "needs-two",
            .matches = {
                SMBIOS_MATCH(SYSTEM_MANUFACTURER, "QEMU"),
                // This ID does not exist in saved state and must fail
                SMBIOS_MATCH(SYSTEM_NAME, "Virtual Machine"),
            },
        },
        {},
    };

    ASSERT_EQ(smbios_match_system(table), (size_t)0);
    ASSERT_EQ(smbios_first_match(table), NULL);
}

TEST_CASE(smbios_match_type_mismatch_fails)
{
    reset_smbios_ids();

    // Save CHASSIS_TYPE as a string instead of a byte on purpose
    set_string_id(SMBIOS_ID_CHASSIS_TYPE, "Desktop");

    smbios_match table[] = {
        {
            .name = "type-mismatch",
            .matches = {
                // This expects a BYTE, but we stored a STRING
                SMBIOS_MATCH_BYTE(CHASSIS_TYPE, 3),
            },
        },
        {},
    };

    ASSERT_EQ(smbios_match_system(table), (size_t)0);
    ASSERT_EQ(smbios_first_match(table), NULL);
}

TEST_CASE(smbios_match_all_four_entries)
{
    const smbios_match *first;

    reset_smbios_ids();

    set_string_id(SMBIOS_ID_SYSTEM_MANUFACTURER, "ACME");
    set_string_id(SMBIOS_ID_SYSTEM_NAME, "UltraBox");
    set_string_id(SMBIOS_ID_SYSTEM_VERSION, "rev1");
    set_string_id(SMBIOS_ID_SYSTEM_SERIAL_NUMBER, "SN123");

    smbios_match table[] = {
        {
            .name = "all-four",
            .matches = {
                SMBIOS_MATCH(SYSTEM_MANUFACTURER, "ACME"),
                SMBIOS_MATCH(SYSTEM_NAME, "UltraBox"),
                SMBIOS_MATCH(SYSTEM_VERSION, "rev1"),
                SMBIOS_MATCH(SYSTEM_SERIAL_NUMBER, "SN123"),
            },
        },
        {},
    };

    ASSERT_EQ(smbios_match_system(table), (size_t)1);
    first = smbios_first_match(table);
    ASSERT_NE(first, NULL);
    ASSERT_STR_EQ(first->name, "all-four");

    // Now drop one of the IDs and verify the rule no longer matches
    reset_smbios_ids();
    set_string_id(SMBIOS_ID_SYSTEM_MANUFACTURER, "ACME");
    set_string_id(SMBIOS_ID_SYSTEM_NAME, "UltraBox");
    set_string_id(SMBIOS_ID_SYSTEM_VERSION, "rev1");
    // SYSTEM_SERIAL_NUMBER intentionally left unset

    ASSERT_EQ(smbios_match_system(table), (size_t)0);
    ASSERT_EQ(smbios_first_match(table), NULL);
}

TEST_CASE(smbios_match_smbios_unavailable)
{
    reset_smbios_ids();
    s_ctx.present = false;

    smbios_match table[] = {
        {
            .name = "any",
            .on_match = counting_match_cb,
            .matches = {
                SMBIOS_MATCH(SYSTEM_MANUFACTURER, "QEMU"),
            },
        },
        {},
    };

    s_match_callback_calls = 0;

    ASSERT_EQ(smbios_match_system(table), (size_t)0);
    ASSERT_EQ(s_match_callback_calls, (size_t)0);
    ASSERT_EQ(smbios_first_match(table), NULL);
}

TEST_CASE(smbios_get_and_has_id)
{
    struct smbios_id id;

    reset_smbios_ids();
    ASSERT_FALSE(smbios_has_id(SMBIOS_ID_SYSTEM_MANUFACTURER));

    set_string_id(SMBIOS_ID_SYSTEM_MANUFACTURER, "ACME");
    ASSERT_TRUE(smbios_has_id(SMBIOS_ID_SYSTEM_MANUFACTURER));
    ASSERT_TRUE(smbios_get_id(SMBIOS_ID_SYSTEM_MANUFACTURER, &id));
    ASSERT_EQ(id.type, SMBIOS_ENTRY_TYPE_STRING);
    ASSERT_STR_EQ(id.str, "ACME");

    reset_smbios_ids();
    set_byte_id(SMBIOS_ID_CHASSIS_TYPE, 10);
    ASSERT_TRUE(smbios_has_id(SMBIOS_ID_CHASSIS_TYPE));
    ASSERT_TRUE(smbios_get_id(SMBIOS_ID_CHASSIS_TYPE, &id));
    ASSERT_EQ(id.type, SMBIOS_ENTRY_TYPE_BYTE);
    ASSERT_EQ(id.byte, 10);
}
