#pragma once

#include <arch/private/cpu.h>

#define INTEL_VFM(family, model) X86_VFM(X86_VENDOR_INTEL, family, model)

/*
 * Models are named INTEL_<MICROARCH><SEGMENT>, where MICROARCH is the code name
 * of the microarchitecture and SEGMENT is the market segment the CPU was
 * designed for and tells apart the models that share it:
 *
 *  (none)    consumer/regular CPU
 *  _MOBILE   mobile CPU
 *  _GFX      CPU with extra graphics features (e.g. extra eDRAM)
 *  _SERVER   server CPU
 *  _MSERVER  micro server CPU
 *
 * NOTE: Later microarchitectures reuse some of these model numbers and differ
 *       only by stepping, e.g. Whiskey Lake is INTEL_KABYLAKE_MOBILE.
 */
#define INTEL_HASWELL INTEL_VFM(6, 0x3C)
#define INTEL_HASWELL_SERVER INTEL_VFM(6, 0x3F)
#define INTEL_HASWELL_MOBILE INTEL_VFM(6, 0x45)
#define INTEL_HASWELL_GFX INTEL_VFM(6, 0x46)

#define INTEL_BROADWELL INTEL_VFM(6, 0x3D)
#define INTEL_BROADWELL_GFX INTEL_VFM(6, 0x47)
#define INTEL_BROADWELL_SERVER INTEL_VFM(6, 0x4F)
#define INTEL_BROADWELL_MSERVER INTEL_VFM(6, 0x56)

#define INTEL_SKYLAKE_MOBILE INTEL_VFM(6, 0x4E)
#define INTEL_SKYLAKE_SERVER INTEL_VFM(6, 0x55)
#define INTEL_SKYLAKE INTEL_VFM(6, 0x5E)

#define INTEL_KABYLAKE_MOBILE INTEL_VFM(6, 0x8E)
#define INTEL_KABYLAKE INTEL_VFM(6, 0x9E)
