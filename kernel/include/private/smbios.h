#pragma once

#include <config.h>

#if IS_ENABLED(SMBIOS)
void smbios_setup(void);
#else
void smbios_setup(void) { }
#endif
