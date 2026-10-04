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

struct duration_unit {
    const char *name;
    u64 ns;
};

static const struct duration_unit s_duration_units[] = {
    { "ns", 1 },
    { "us", NS_PER_US },
    { "ms", NS_PER_MS },
    { "s", NS_PER_SEC },
    { "min", MS_PER_MIN * NS_PER_MS },
    { "h", MS_PER_HOUR * NS_PER_MS },
};

void duration_to_human(u64 ns, struct human_repr *out_human_repr)
{
    const struct duration_unit *unit;
    size_t i = 0;

    while (i < (ARRAY_SIZE(s_duration_units) - 1) &&
           ns >= s_duration_units[i + 1].ns)
        i++;

    unit = &s_duration_units[i];
    out_human_repr->value = ns / unit->ns;
    out_human_repr->thousandths = ((ns % unit->ns) * 1000) / unit->ns;
    out_human_repr->unit = unit->name;
}
