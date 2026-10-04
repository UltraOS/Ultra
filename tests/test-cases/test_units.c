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

static void check_duration(
    u64 ns, u64 value, u64 thousandths, const char *unit
)
{
    struct human_repr duration;

    duration_to_human(ns, &duration);

    ASSERT_EQ(duration.value, value);
    ASSERT_EQ(duration.thousandths, thousandths);
    ASSERT_STR_EQ(duration.unit, unit);
}

TEST_CASE(duration_below_a_microsecond_stays_in_nanoseconds)
{
    check_duration(0, 0, 0, "ns");
    check_duration(999, 999, 0, "ns");
}

TEST_CASE(duration_is_expressed_in_the_largest_unit_it_reaches)
{
    check_duration(1500, 1, 500, "us");
    check_duration(2343 * NS_PER_MS, 2, 343, "s");
    check_duration(90 * NS_PER_SEC, 1, 500, "min");
    check_duration(3 * MS_PER_HOUR * NS_PER_MS, 3, 0, "h");
}

TEST_CASE(duration_fraction_is_truncated)
{
    check_duration(1999999, 1, 999, "ms");
    check_duration(549800 * NS_PER_MS, 9, 163, "min");
}

TEST_CASE(duration_stops_at_hours)
{
    check_duration(48 * MS_PER_HOUR * NS_PER_MS, 48, 0, "h");
}
