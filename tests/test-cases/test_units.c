#include <time/units.h>

#include <test_harness.h>

static void check(u64 hz, u64 value, u64 thousandths, const char *unit)
{
    struct human_repr freq;

    frequency_to_human(hz, &freq);

    ASSERT_EQ(freq.value, value);
    ASSERT_EQ(freq.thousandths, thousandths);
    ASSERT_STR_EQ(freq.unit, unit);
}

TEST_CASE(frequency_below_a_kilohertz_stays_in_hertz)
{
    check(0, 0, 0, "Hz");
    check(999, 999, 0, "Hz");
}

TEST_CASE(frequency_is_expressed_in_the_largest_unit_it_reaches)
{
    check(1000, 1, 0, "kHz");
    check(32768, 32, 768, "kHz");
    check(24 * MHZ, 24, 0, "MHz");
    check(2995200000, 2, 995, "GHz");
}

TEST_CASE(frequency_fraction_is_truncated)
{
    check(1000145999, 1, 0, "GHz");
    check(1999999, 1, 999, "MHz");
}

TEST_CASE(frequency_stops_at_gigahertz)
{
    check(12345 * GHZ, 12345, 0, "GHz");
}
