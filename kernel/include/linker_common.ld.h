#include <common/helpers.h>

#include <linker.h>

#include <arch/constants.h>
#include <private/init_level.h>

#define PHDR_READ  (1 << 2)
#define PHDR_WRITE (1 << 1)
#define PHDR_EXEC  (1 << 0)

#define VIRTUAL_BASE_RELATIVE(type) AT (ADDR (type) - VIRTUAL_BASE)

#define SPECIAL_SECTION(name) KEEP(*(.name))

#define MARKED_SECTION(name)        \
    SECTION_MARKER_BEGIN(name) = .; \
    SPECIAL_SECTION(name)           \
    SECTION_MARKER_END(name) = .;

#define ALIGNED_MARKED_SECTION(name, align) \
    . = ALIGN(align);                       \
    MARKED_SECTION(name)                    \

#define NATURALLY_ALIGNED_MARKED_SECTION(name) \
    ALIGNED_MARKED_SECTION(name, ULTRA_ARCH_WIDTH)

#define FREE_AFTER_INIT_BEGIN(section) \
    . = ALIGN(PAGE_SIZE); \
    SECTION_MARKER_BEGIN(section) = .;

#define FREE_AFTER_INIT_END(section) \
    . = ALIGN(PAGE_SIZE); \
    SECTION_MARKER_END(section) = .;

#define TEXT_BEGIN LINKER_SYMBOL(text_begin) = .;
#define TEXT_END LINKER_SYMBOL(text_end) = .;

#define TEXT  \
    *(.text)  \
    *(.text.*) \
    *(.INIT_DATA_REFERENCE_TEXT_SECTION)

#define TEXT_FREE_AFTER_INIT_BEGIN \
    FREE_AFTER_INIT_BEGIN(FREE_AFTER_INIT_TEXT_SECTION)

#define TEXT_FREE_AFTER_INIT_END \
    FREE_AFTER_INIT_END(FREE_AFTER_INIT_TEXT_SECTION)

#define TEXT_FREE_AFTER_INIT \
    *(.FREE_AFTER_INIT_TEXT_SECTION) \

#define TEXT_OUTPUT                      \
    .text : VIRTUAL_BASE_RELATIVE(.text) \
    {                                    \
        TEXT_BEGIN                       \
        TEXT                             \
        TEXT_FREE_AFTER_INIT_BEGIN       \
        TEXT_FREE_AFTER_INIT             \
        TEXT_FREE_AFTER_INIT_END         \
        TEXT_END                         \
    } :text

#define INIT_LEVEL(x)                                               \
    NATURALLY_ALIGNED_MARKED_SECTION(INIT_LEVEL_CB_SECTION(x, pre)) \
    NATURALLY_ALIGNED_MARKED_SECTION(INIT_LEVEL_CB_SECTION(x, post))

#define INIT_LEVEL_CB_SECTIONS INIT_LEVELS

#define RODATA                                                 \
    *(.rodata .rodata.*)                                       \
    *(.INIT_DATA_REFERENCE_RODATA_SECTION)                     \
    NATURALLY_ALIGNED_MARKED_SECTION(EARLY_PARAMETERS_SECTION) \
    NATURALLY_ALIGNED_MARKED_SECTION(PARAMETERS_SECTION)

#define RODATA_FREE_AFTER_INIT_BEGIN \
    FREE_AFTER_INIT_BEGIN(FREE_AFTER_INIT_RODATA_SECTION)

#define RODATA_FREE_AFTER_INIT_END \
    FREE_AFTER_INIT_END(FREE_AFTER_INIT_RODATA_SECTION)

#define RODATA_FREE_AFTER_INIT \
    *(.FREE_AFTER_INIT_RODATA_SECTION) \
    INIT_LEVEL_CB_SECTIONS

#define RODATA_OUTPUT                                                     \
    .rodata : VIRTUAL_BASE_RELATIVE(.rodata)                              \
    {                                                                     \
        RODATA                                                            \
        NATURALLY_ALIGNED_MARKED_SECTION(ABORTABLE_INSTRUCTIONS_SECTION)  \
        RODATA_FREE_AFTER_INIT_BEGIN                                      \
        RODATA_FREE_AFTER_INIT                                            \
        RODATA_FREE_AFTER_INIT_END                                        \
    } :rodata =0xDEADBEEF

#define EH_FRAME_HDR                       \
    LINKER_SYMBOL(eh_frame_hdr_begin) = .; \
    *(.eh_frame_hdr)                       \
    LINKER_SYMBOL(eh_frame_hdr_end) = .;

#define EH_FRAME_HDR_OUTPUT                              \
    .eh_frame_hdr : VIRTUAL_BASE_RELATIVE(.eh_frame_hdr) \
    {                                                    \
        EH_FRAME_HDR                                     \
    } :rodata =0xDEADBEEF

#define EH_FRAME_OUTPUT                          \
    .eh_frame : VIRTUAL_BASE_RELATIVE(.eh_frame) \
    {                                            \
        EH_FRAME                                 \
    } :rodata =0xDEADBEEF

#define EH_FRAME                       \
    LINKER_SYMBOL(eh_frame_begin) = .; \
    *(.eh_frame)                       \
    LINKER_SYMBOL(eh_frame_end) = .;

#define DATA \
    *(.data) \
    *(.data.*) \
    *(.INIT_DATA_REFERENCE_DATA_SECTION)

#define DATA_FREE_AFTER_INIT_BEGIN \
    FREE_AFTER_INIT_BEGIN(FREE_AFTER_INIT_DATA_SECTION)

#define DATA_FREE_AFTER_INIT_END \
    FREE_AFTER_INIT_END(FREE_AFTER_INIT_DATA_SECTION)

#define DATA_FREE_AFTER_INIT \
    MARKED_SECTION(PER_CPU_SECTION) \
    *(.FREE_AFTER_INIT_DATA_SECTION)

#define DATA_OUTPUT                      \
    .data : VIRTUAL_BASE_RELATIVE(.data) \
    {                                    \
        DATA                             \
        DATA_FREE_AFTER_INIT_BEGIN       \
        DATA_FREE_AFTER_INIT             \
        DATA_FREE_AFTER_INIT_END         \
    } :data

#define BSS_OUTPUT                     \
    .bss : VIRTUAL_BASE_RELATIVE(.bss) \
    {                                  \
        BSS                            \
    } :data

#define BSS        \
    *(COMMON)      \
    *(.bss .bss.*)

#define SECTION_TABS                     \
    .symtab : { *(.symtab) }             \
    .strtab : { *(.strtab) }             \
    .shstrtab : { *(.shstrtab) }         \
    .symtab_shndx : { *(.symtab_shndx) } \
    ASSERT(SIZEOF(.symtab_shndx) == 0, "Too many sections(?)")

#define SECTION_DEBUG                                                       \
    .debug_str         0x000000000 : { *(.debug_str) }                      \
    .debug_str_offsets 0x000000000 : { *(.debug_str_offsets) }              \
    .debug_abbrev      0x000000000 : { *(.debug_abbrev) }                   \
    .debug_info        0x000000000 : { *(.debug_info) }                     \
    .debug_frame       0x000000000 : { *(.debug_frame) }                    \
    .debug_line        0x000000000 : { *(.debug_line) }                     \
    .debug_line_str    0x000000000 : { *(.debug_line_str) }                 \
    .debug_loc         0x000000000 : { *(.debug_loc) }                      \
    .debug_addr        0x000000000 : { *(.debug_addr) }                     \
    .debug_macro       0x000000000 : { *(.debug_macro) *(.debug_macro[*]) } \
    .debug_aranges     0x000000000 : { *(.debug_aranges) }                  \
    .debug_loclists    0x000000000 : { *(.debug_loclists) }                 \
    .debug_rnglists    0x000000000 : { *(.debug_rnglists) }

#define DISCARDS              \
    /DISCARD/ : {             \
        *(.comment)           \
        *(.note.GNU-stack)    \
        *(.note.gnu.build-id) \
    }

#define EXPECT_EMPTY_RELOC(type, input)                                             \
    type : { input }                                                                \
    ASSERT(SIZEOF(type) == 0, TO_STR(Unexpected non-empty relocation section type))

// A guard for when a binary doesn't expect relocations to be generated
#define NORELOCS_GUARD                                    \
    EXPECT_EMPTY_RELOC(.got.plt, *(.got.plt))             \
    EXPECT_EMPTY_RELOC(.got, *(.got) *(.igot.*))          \
    EXPECT_EMPTY_RELOC(.plt, *(.plt) *(.plt.*) *(.iplt))  \
    EXPECT_EMPTY_RELOC(.rel, *(.rel.*) *(.rel_*))         \
    EXPECT_EMPTY_RELOC(.rela, *(.rela.*) *(.rela_*) )

#define DATA_MARKER(name) \
    . = ALIGN(PAGE_SIZE); \
    LINKER_SYMBOL(name) = .;

#define EXECUTABLE_DATA_BEGIN DATA_MARKER(executable_data_begin)
#define EXECUTABLE_DATA_END DATA_MARKER(executable_data_end)

#define READONLY_DATA_BEGIN DATA_MARKER(readonly_data_begin)
#define READONLY_DATA_END DATA_MARKER(readonly_data_end)

#define READWRITE_DATA_BEGIN DATA_MARKER(readwrite_data_begin)
#define READWRITE_DATA_END DATA_MARKER(readwrite_data_end)
