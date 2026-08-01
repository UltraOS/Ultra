#include <common/helpers.h>

#include <memory/units.h>

static const char *const s_units[] = {
    "B", "KiB", "MiB", "GiB", "TiB", "PiB"
};

static const char *const s_short_units[] = {
    "B", "K", "M", "G", "T", "P"
};

static void do_size_to_human(
    size_t size, bool short_units, struct human_size *out_size
)
{
    size_t i = 0, rem = 0;

    /*
     * 'rem' trails one division behind, so when the loop stops it holds the
     * part of 'size' that didn't survive the last promotion. That is exactly
     * the fraction we want to report, in units of the level below.
     */
    while (i < (ARRAY_SIZE(s_units) - 1) && size >= KiB) {
        rem = size % KiB;
        size /= KiB;
        i++;
    }

    out_size->value = size;
    out_size->hundredths = (rem * 100) / KiB;
    out_size->unit = short_units ? s_short_units[i] : s_units[i];
}

void size_to_human(size_t size, struct human_size *out_human_size)
{
    do_size_to_human(size, false, out_human_size);
}

void size_to_human_short(size_t size, struct human_size *out_human_size)
{
    do_size_to_human(size, true, out_human_size);
}
