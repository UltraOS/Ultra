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

/*
 * The harness physical backing store is tiny, so this only has to be a
 * plausible ceiling for the range checks that consult it. 52 is what x86_64
 * reports with 5-level paging, matching the backend in arch/page_table.h.
 */
#define MAX_PHYS_BITS 52
