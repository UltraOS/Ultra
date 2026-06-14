#pragma once

#include <common/helpers.h>
#include <config_helpers.h>

#include <generated/config.h>

#ifdef CONFIG_MAX_CPUS
#define ULTRA_MAX_CPUS CONFIG_MAX_CPUS
#else
#define ULTRA_MAX_CPUS 1
#endif

