#include <refcount.h>

#include <test_harness.h>

#define ASSERT_GET_PUT(ref) do {                  \
        refcount_set(ref, 1);                     \
        refcount_inc(ref);                        \
        ASSERT_EQ(refcount_read(ref), 2);         \
        ASSERT_FALSE(refcount_dec_and_test(ref)); \
        ASSERT_TRUE(refcount_dec_and_test(ref));  \
        ASSERT_EQ(refcount_read(ref), 0);         \
    } while (0)

TEST_CASE(refcount_get_put)
{
    struct refcount32 ref32;
    struct refcount64 ref64;

    ASSERT_GET_PUT(&ref32);
    ASSERT_GET_PUT(&ref64);
}

TEST_CASE(refcount_init_and_const_read)
{
    struct refcount32 ref32 = REFCOUNT_INIT(1);
    const struct refcount64 ref64 = REFCOUNT_INIT(3);

    ASSERT_EQ(refcount_read(&ref32), 1);
    ASSERT_EQ(refcount_read(&ref64), 3);
}

TEST_CASE(refcount_inc_on_zero_saturates)
{
    struct refcount32 ref32 = REFCOUNT_INIT(0);
    struct refcount64 ref64 = REFCOUNT_INIT(0);

    refcount_inc(&ref32);
    refcount_inc(&ref64);
    ASSERT_EQ(refcount_read(&ref32), REFCOUNT32_SATURATED);
    ASSERT_EQ(refcount_read(&ref64), REFCOUNT64_SATURATED);

    ASSERT_FALSE(refcount_dec_and_test(&ref32));
    ASSERT_FALSE(refcount_dec_and_test(&ref64));
}

TEST_CASE(refcount_underflow_saturates)
{
    struct refcount32 ref32 = REFCOUNT_INIT(0);
    struct refcount64 ref64 = REFCOUNT_INIT(0);

    ASSERT_FALSE(refcount_dec_and_test(&ref32));
    ASSERT_FALSE(refcount_dec_and_test(&ref64));
    ASSERT_EQ(refcount_read(&ref32), REFCOUNT32_SATURATED);
    ASSERT_EQ(refcount_read(&ref64), REFCOUNT64_SATURATED);
}

TEST_CASE(refcount32_overflow_saturates)
{
    struct refcount32 ref = REFCOUNT_INIT(REFCOUNT32_MAX);

    refcount_inc(&ref);
    ASSERT_EQ(refcount_read(&ref), REFCOUNT32_SATURATED);

    // A saturated count stays put
    refcount_inc(&ref);
    ASSERT_EQ(refcount_read(&ref), REFCOUNT32_SATURATED);
    ASSERT_FALSE(refcount_dec_and_test(&ref));
    ASSERT_EQ(refcount_read(&ref), REFCOUNT32_SATURATED);
}

TEST_CASE(refcount64_skips_the_overflow_check)
{
    struct refcount64 ref = REFCOUNT_INIT(REFCOUNT64_MAX);

    refcount_inc(&ref);
    ASSERT_EQ(refcount_read(&ref), REFCOUNT64_MAX + 1);
}
