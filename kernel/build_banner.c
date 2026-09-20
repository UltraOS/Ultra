#include <config.h>
#include <build_banner.h>

#define IN_BUILD_BANNER
#include <generated/build_stamp.h>

#ifdef CONFIG_TOOLCHAIN_CLANG
#define BUILD_COMPILER_VERSION __clang_version__
#else
#define BUILD_COMPILER_VERSION __VERSION__
#endif

const char g_build_banner[] =
    "Starting ultra kernel v" ULTRA_VERSION " on " ULTRA_ARCH
    " (@" ULTRA_BUILD_REVISION ", " CONFIG_TOOLCHAIN_STRING " "
    BUILD_COMPILER_VERSION ", built on " ULTRA_BUILD_DATE ")";
