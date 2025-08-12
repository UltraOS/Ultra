#include <common/helpers.h>

#include <linker.h>

#define PHDR_READ  (1 << 2)
#define PHDR_WRITE (1 << 1)
#define PHDR_EXEC  (1 << 0)

#define VIRTUAL_BASE_RELATIVE(type) AT (ADDR (type) - VIRTUAL_BASE)

#define SPECIAL_SECTION(name) KEEP(*(name))

#define MARKED_SECTION(name)                 \
    CONCAT(LINKER_SYMBOL(name), _begin) = .; \
    SPECIAL_SECTION(name)                    \
    CONCAT(LINKER_SYMBOL(name), _end) = .;

#define TEXT_BEGIN LINKER_SYMBOL(text_begin) = .;
#define TEXT_END LINKER_SYMBOL(text_end) = .;

#define TEXT  \
    *(.text)  \
    *(.text.*)

#define INITCALLS                  \
    MARKED_SECTION(initcall_normal)

#define RODATA(align)                        \
    *(.rodata .rodata.*)                     \
    . = ALIGN(align);                        \
    MARKED_SECTION(EARLY_PARAMETERS_SECTION) \
    MARKED_SECTION(PARAMETERS_SECTION)       \
    INITCALLS

#define EH_FRAME_HDR                       \
    LINKER_SYMBOL(eh_frame_hdr_begin) = .; \
    *(.eh_frame_hdr)                       \
    LINKER_SYMBOL(eh_frame_hdr_end) = .;

#define EH_FRAME                       \
    LINKER_SYMBOL(eh_frame_begin) = .; \
    *(.eh_frame)                       \
    LINKER_SYMBOL(eh_frame_end) = .;

#define DATA \
    *(.data) \
    *(.data.*)

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

#define DISCARDS     \
    /DISCARD/ : {    \
        *(.comment)  \
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
