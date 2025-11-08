#define MSG_FMT(msg) "per-cpu: " msg

#include <boot/alloc.h>

#include <linker.h>
#include <per_cpu.h>
#include <log.h>
#include <bug.h>
#include <io.h>

#include <common/string.h>
#include <common/error.h>
#include <common/align.h>

#include <private/per_cpu.h>

ptr_t g_per_cpu_offset[ULTRA_MAX_CPUS];

extern u8 SECTION_MARKER_BEGIN(PER_CPU_SECTION)[];
extern u8 SECTION_MARKER_END(PER_CPU_SECTION)[];
