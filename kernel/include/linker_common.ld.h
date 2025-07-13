#include <common/helpers.h>

#define PHDR_READ  (1 << 2)
#define PHDR_WRITE (1 << 1)
#define PHDR_EXEC  (1 << 0)

#define VIRTUAL_BASE_RELATIVE(type) AT (ADDR (type) - VIRTUAL_BASE)

#define TEXT_BEGIN g_linker_symbol_text_begin = .;
#define TEXT_END g_linker_symbol_text_end = .;

#define TEXT  \
    *(.text)  \
    *(.text.*)

#define INITCALLS                                 \
    g_linker_symbol_initcalls_earlycon_begin = .; \
    KEEP(*(.initcall_earlycon))                   \
    g_linker_symbol_initcalls_earlycon_end = .;   \
                                                  \
    g_linker_symbol_initcalls_normal_begin = .;   \
    KEEP(*(.initcall_normal))                     \
    g_linker_symbol_initcalls_normal_end = .;

#define RODATA(init_align)  \
    *(.rodata .rodata.*)    \
    . = ALIGN(init_align);  \
    INITCALLS

#define EH_FRAME_HDR                         \
    g_linker_symbol_eh_frame_hdr_begin = .;  \
    *(.eh_frame_hdr)                         \
    g_linker_symbol_eh_frame_hdr_end = .;

#define EH_FRAME                        \
    g_linker_symbol_eh_frame_begin = .; \
    *(.eh_frame)                        \
    g_linker_symbol_eh_frame_end = .;

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
