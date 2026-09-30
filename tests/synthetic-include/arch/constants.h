#pragma once

/*
 * Undef first: host headers may leak their own definitions into harness
 * translation units, e.g. the mach headers on arm64 macOS, where the host
 * page size is 16K.
 */
#undef PAGE_SIZE
#undef PAGE_SHIFT

#define PAGE_SIZE 4096
#define PAGE_SHIFT 12
