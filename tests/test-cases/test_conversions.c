#include <common/conversions.h>
#include <pci/address.h>

#include <test_harness.h>

TEST_CASE(conversions_empty_is_not_a_number)
{
    u64 value = 1;
    i64 signed_value = 1;

    ASSERT_NE(str_to_u64(STR(""), &value), EOK);
    ASSERT_NE(str_to_u64_with_base(STR(""), &value, 10), EOK);
    ASSERT_NE(str_to_u64_with_base(STR(""), &value, 16), EOK);
    ASSERT_NE(str_to_u64(STR("+"), &value), EOK);
    ASSERT_NE(str_to_u64(STR("0x"), &value), EOK);
    ASSERT_NE(str_to_u64(STR("0b"), &value), EOK);
    ASSERT_NE(str_to_i64(STR("-"), &signed_value), EOK);
    ASSERT_NE(str_to_i64_with_base(STR("-"), &signed_value, 10), EOK);
    ASSERT_EQ(value, 1);
    ASSERT_EQ(signed_value, 1);
}

TEST_CASE(conversions_zero)
{
    u64 value = 1;

    ASSERT_EQ(str_to_u64(STR("0"), &value), EOK);
    ASSERT_EQ(value, 0);
    ASSERT_EQ(str_to_u64(STR("00"), &value), EOK);
    ASSERT_EQ(value, 0);
    ASSERT_EQ(str_to_u64(STR("0x0"), &value), EOK);
    ASSERT_EQ(value, 0);
    ASSERT_EQ(str_to_u64_with_base(STR("0"), &value, 16), EOK);
    ASSERT_EQ(value, 0);
}

TEST_CASE(conversions_bases)
{
    u64 value;

    ASSERT_EQ(str_to_u64(STR("255"), &value), EOK);
    ASSERT_EQ(value, 255);
    ASSERT_EQ(str_to_u64(STR("0xff"), &value), EOK);
    ASSERT_EQ(value, 255);
    ASSERT_EQ(str_to_u64(STR("0377"), &value), EOK);
    ASSERT_EQ(value, 255);
    ASSERT_EQ(str_to_u64(STR("0b11111111"), &value), EOK);
    ASSERT_EQ(value, 255);
    ASSERT_EQ(str_to_u64_with_base(STR("ff"), &value, 16), EOK);
    ASSERT_EQ(value, 255);
}

TEST_CASE(conversions_digits_outside_the_base)
{
    u64 value;

    ASSERT_NE(str_to_u64(STR("1a"), &value), EOK);
    ASSERT_NE(str_to_u64(STR("08"), &value), EOK);
    ASSERT_NE(str_to_u64(STR("0b12"), &value), EOK);
    ASSERT_NE(str_to_u64_with_base(STR("9"), &value, 8), EOK);
    ASSERT_NE(str_to_u64(STR("12x"), &value), EOK);
}

static void assert_parses(
    const char *text, u16 segment, u8 bus, u8 device, u8 function
)
{
    struct pci_address addr;

    ASSERT_EQ(str_to_pci_address(STR_RUNTIME(text), &addr), EOK);
    ASSERT_EQ(addr.segment, segment);
    ASSERT_EQ(addr.bus, bus);
    ASSERT_EQ(addr.device, device);
    ASSERT_EQ(addr.function, function);
}

static void assert_rejects(const char *text)
{
    struct pci_address addr;

    ASSERT_NE(str_to_pci_address(STR_RUNTIME(text), &addr), EOK);
}

TEST_CASE(conversions_pci_address_bus_device_function)
{
    assert_parses("0:16.3", 0, 0x00, 0x16, 3);
    assert_parses("00:16.3", 0, 0x00, 0x16, 3);
    assert_parses("ff:1f.7", 0, 0xFF, 0x1F, 7);
    assert_parses("0:0.0", 0, 0, 0, 0);
}

TEST_CASE(conversions_pci_address_with_segment)
{
    assert_parses("0000:00:16.3", 0, 0x00, 0x16, 3);
    assert_parses("1:0:5.0", 1, 0x00, 0x05, 0);
    assert_parses("ffff:ff:1f.7", 0xFFFF, 0xFF, 0x1F, 7);
}

TEST_CASE(conversions_pci_address_malformed)
{
    assert_rejects("");
    assert_rejects("16.3");
    assert_rejects("0:16");
    assert_rejects("0:16.");
    assert_rejects(":16.3");
    assert_rejects("0:16.3.1");
    assert_rejects("0:1g.3");
    assert_rejects("0:16.3 ");
    assert_rejects("0:0:0:0.0");
    assert_rejects(":0:16.3");
    assert_rejects("0::16.3");
    assert_rejects("0:.3");
}

TEST_CASE(conversions_pci_address_out_of_range)
{
    assert_rejects("0:20.0");
    assert_rejects("0:0.8");
    assert_rejects("100:0.0");
    assert_rejects("10000:0:0.0");
}
