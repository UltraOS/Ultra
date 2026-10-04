#include <common/helpers.h>

#include <time/units.h>

static const char *const s_units[] = {
    "Hz", "kHz", "MHz", "GHz"
};

void frequency_to_human(u64 hz, struct human_repr *out_human_repr)
{
    u64 rem = 0;
    size_t i = 0;

    while (i < (ARRAY_SIZE(s_units) - 1) && hz >= KHZ) {
        rem = hz % KHZ;
        hz /= KHZ;
        i++;
    }

    out_human_repr->value = hz;
    out_human_repr->thousandths = rem;
    out_human_repr->unit = s_units[i];
}
