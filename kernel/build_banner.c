#include <config.h>
#include <build_banner.h>

#define IN_BUILD_BANNER
#include <generated/build_stamp.h>

const char g_build_banner[] =
    "Starting ultra kernel v" ULTRA_VERSION " on " ULTRA_ARCH
    " (@" ULTRA_BUILD_REVISION ", built on " ULTRA_BUILD_DATE ")";
