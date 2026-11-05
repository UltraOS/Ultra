#pragma once

#include <param.h>
#include <linker.h>

DECLARE_TYPED_SECTION_MARKERS(struct param, PARAMETERS_SECTION);
DECLARE_TYPED_SECTION_MARKERS(struct param, FREE_AFTER_INIT_PARAMETERS_SECTION);

struct param_table {
    struct param *params;
    size_t count;
};

/*
 * Parses the given command line against the given parameter tables, searched
 * in order, and returns the string (if any) after --. Unknown parameters and
 * bad values are logged. cmdline_parse is the single table form.
 */
struct string cmdline_parse_tables(
    struct string cmdline, const struct param_table *tables, size_t num_tables
);
struct string cmdline_parse(
    struct string cmdline, struct param *params, size_t num_params
);
