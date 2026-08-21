#include <common/bit.h>

reg_t find_next_bit_base(
    const reg_t *bit_array, reg_t num_bits, reg_t start, bool inverted
)
{
    reg_t first_unit, num_units, extra_bits, i;

    if (start >= num_bits)
        return num_bits;

    first_unit = BIT_UNIT(start);
    num_units = NUM_UNITS_FOR_BITS(num_bits);
    extra_bits = num_bits % BITS_PER_UNIT;

    for (i = first_unit; i < num_units; i++) {
        reg_t unit, start_idx, bit;

        unit = bit_array[i];

        /*
         * If looking for 0s, invert the unit before masking so out-of-bounds
         * zeros don't accidentally become 1s and trigger false positives
         */
        if (inverted)
            unit = ~unit;

        // Mask out bits before the 'start' index in the first unit
        if (i == first_unit) {
            start_idx = BIT_INDEX(start);
            if (start_idx > 0)
                unit &= MAKE_BIT_MASK(BITS_PER_UNIT - 1, start_idx);
        }

        // Mask out bits beyond 'num_bits' in the last unit
        if (i == num_units - 1 && extra_bits != 0)
            unit &= MAKE_BIT_MASK(extra_bits - 1, 0);

        // If there are any valid 1s left, find the lowest one
        if (unit != 0) {
            bit = BIT_UNIT_FIND_FIRST_SET(unit) - 1;
            return (i * BITS_PER_UNIT) + bit;
        }
    }

    return num_bits;
}

/*
 * Baseline x86_64 has no popcount instruction, and GCC lowers
 * __builtin_popcountl to a libgcc call we cannot link against in debug
 * builds, so count by hand.
 */
static reg_t bit_unit_count_set(reg_t unit)
{
    reg_t count = 0;

    while (unit != 0) {
        unit &= unit - 1;
        count++;
    }

    return count;
}

reg_t bit_array_count_set(const reg_t *bit_array, reg_t num_bits)
{
    reg_t num_units, extra_bits, count = 0, i;

    num_units = NUM_UNITS_FOR_BITS(num_bits);
    extra_bits = num_bits % BITS_PER_UNIT;

    for (i = 0; i < num_units; i++) {
        reg_t unit;

        unit = bit_array[i];

        // Mask out bits beyond 'num_bits' in the last unit
        if (i == num_units - 1 && extra_bits != 0)
            unit &= MAKE_BIT_MASK(extra_bits - 1, 0);

        count += bit_unit_count_set(unit);
    }

    return count;
}
