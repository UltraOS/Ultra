#pragma once

#include <common/helpers.h>
#include <common/attributes.h>
#include <linker.h>

/*
 * Markers for data or code that are freed after kernel initialization:
 * INIT_CODE -> marks a function to be freed after init
 * INIT_DATA -> marks a variable to be freed after init
 * INIT_RODATA -> marks a const variable to be freed after init
 */
#define INIT_CODE SECTION(FREE_AFTER_INIT_TEXT_SECTION)
#define INIT_DATA SECTION(FREE_AFTER_INIT_DATA_SECTION)
#define INIT_RODATA SECTION(FREE_AFTER_INIT_RODATA_SECTION)

/*
 * By default, only INIT_* variables and functions can reference other INIT_*
 * variables and functions, but sometimes it is required that a non-init piece
 * of code or data makes such references.
 *
 * A common example is something like:
 *     void* INIT_CODE foo_alloc_early(void) { ... }
 *
 *     void* CODE_REFERENCES_INIT_DATA foo_alloc(void) {
 *         if (init_level_below(INIT_LEVEL_FOO_AVAILABLE))
 *              return foo_alloc_early();
 *
 *         return foo_alloc_late();
 *     }
 *
 *     foo_alloc must be marked as CODE_REFERENCES_INIT_DATA due to calling
 *     into foo_alloc_early in this example.
 *
 * CODE_REFERENCES_INIT_DATA -> allows a runtime function to reference a
 *                              freed-after-init piece of data
 * DATA_REFERENCES_INIT_DATA -> allows a runtime variable to reference a
 *                              freed-after-init piece of data
 * RODATA_REFERENCES_INIT_DATA -> allows a constant variable to reference
 *                                a freed-after-init piece of data
 */
#define CODE_REFERENCES_INIT_DATA \
    SECTION(INIT_DATA_REFERENCE_TEXT_SECTION) NEVER_INLINE
#define DATA_REFERENCES_INIT_DATA SECTION(INIT_DATA_REFERENCE_DATA_SECTION)
#define RODATA_REFERENCES_INIT_DATA SECTION(INIT_DATA_REFERENCE_RODATA_SECTION)
