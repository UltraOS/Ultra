#pragma once

#include <config.h>

#if IS_ENABLED(ACPI)

void acpi_setup_tables(void);

#else

static inline void acpi_setup_tables(void) { }

#endif
