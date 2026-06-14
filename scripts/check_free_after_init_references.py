#!/usr/bin/python3
import sys
import argparse
from typing import List, Dict, Any, Optional, Tuple

try:
    from elftools.elf.elffile import ELFFile
    from elftools.elf.relocation import RelocationSection
    from elftools.elf.sections import Section, SymbolTableSection
except Exception as ex:
    print(f"Skipped: {ex}")
    sys.exit(0)

TARGET_PREFIX: str = '.free_after_init'

ALLOWED_PREFIXES: Tuple[str, ...] = (
    TARGET_PREFIX,
    '.text.init_data_reference',
    '.data.init_data_reference',
    '.rodata.init_data_reference'
)

IGNORED_PREFIXES: Tuple[str, ...] = (
    '.debug_',
    '.eh_frame',
    '.discard',
)

SymMap = Dict[int, List[Dict[str, Any]]]


def check_reference_section(reference_name: str) -> bool:
    return (any(reference_name.startswith(p) for p in IGNORED_PREFIXES) or
            any(reference_name.startswith(p) for p in ALLOWED_PREFIXES))


def format_sym(name: Optional[str], sym_type: str) -> str:
    if not name:
        return "<anonymous>"
    if sym_type == 'STT_FUNC':
        return f"{name}()"
    elif sym_type == 'STT_OBJECT':
        return f"(variable) {name}"
    elif sym_type == 'STT_SECTION':
        return "<section chunk>"
    return name


def build_symbol_data(
    symtab: Any, sec_names: List[str]
) -> Tuple[SymMap, List[bool]]:
    sym_map: SymMap = {}
    sym_is_init: List[bool] = []

    for symbol in symtab.iter_symbols():
        st_type: str = symbol['st_info']['type']
        shndx = symbol['st_shndx']

        # Does this symbol point to a .free_after_init section?
        is_init = False
        if isinstance(shndx, int) and shndx < len(sec_names):
            if sec_names[shndx].startswith(TARGET_PREFIX):
                is_init = True
        sym_is_init.append(is_init)

        if isinstance(shndx, str):
            continue

        value: int = symbol['st_value']

        # Symbol mapping: lookup offset to find the bounding function
        if st_type in ('STT_FUNC', 'STT_OBJECT') and symbol.name:
            if shndx not in sym_map:
                sym_map[shndx] = []
            sym_map[shndx].append({
                'name': symbol.name,
                'type': st_type,
                'start': value,
                'size': symbol['st_size'],
            })

    # Sort offsets sequentially for boundary searching
    for idx in sym_map:
        sym_map[idx].sort(key=lambda x: x['start'])

    return sym_map, sym_is_init


def find_calling_symbol(
    sym_map: SymMap, section_idx: int, offset: int
) -> Dict[str, Any]:
    fallback = {
        'name': '<unknown symbol>',
        'type': 'STT_NOTYPE',
        'start': 0,
        'size': 0
    }

    if section_idx not in sym_map:
        return fallback

    symbols = sym_map[section_idx]
    low = 0
    high = len(symbols) - 1
    best_idx = -1

    while low <= high:
        mid = (low + high) // 2
        if symbols[mid]['start'] <= offset:
            best_idx = mid
            low = mid + 1
        else:
            high = mid - 1

    if best_idx != -1:
        sym = symbols[best_idx].copy()

        if offset >= (sym['start'] + sym['size']):
            # Make sure it's clear that this is just a best guess if we're
            # out-of-bounds of the symbol and there wasn't a better option
            sym['name'] += " (best guess)"
        return sym

    return fallback


def find_illegal_free_after_init_references(elf: ELFFile) -> int:
    symtab: Optional[Section] = elf.get_section_by_name('.symtab')
    if not symtab:
        sys.exit("No symbol table found in file")

    assert isinstance(symtab, SymbolTableSection)

    sec_names: List[str] = [sec.name for sec in elf.iter_sections()]
    sym_map, sym_is_init = build_symbol_data(symtab, sec_names)

    illegal_refs_found: int = 0

    for section in elf.iter_sections():
        if not isinstance(section, RelocationSection):
            continue

        reference_idx: int = section.header['sh_info']
        if reference_idx < len(sec_names):
            reference_name: str = sec_names[reference_idx]
        else:
            reference_name = "<unknown section>"

        if check_reference_section(reference_name):
            continue

        for reloc in section.iter_relocations():
            sym_idx: int = reloc['r_info_sym']

            if sym_idx == 0 or not sym_is_init[sym_idx]:
                continue

            target_sym = symtab.get_symbol(sym_idx)
            target_idx = target_sym['st_shndx']

            if isinstance(target_idx, str):
                continue

            r_off: int = reloc['r_offset']
            call_info = find_calling_symbol(sym_map, reference_idx, r_off)
            call_str = format_sym(call_info['name'], call_info['type'])

            t_type: str = target_sym['st_info']['type']
            t_off: int = target_sym['st_value']
            if reloc.is_RELA():
                t_off += reloc['r_addend']

            target_func = find_calling_symbol(sym_map, target_idx, t_off)
            func_start = target_func['start']
            func_end = func_start + target_func['size']

            is_jump_table = (
                t_type == 'STT_SECTION' and
                func_start < t_off < func_end
            ) and reference_name.startswith('.rodata')

            if is_jump_table:
                continue

            t_str: str = format_sym(target_sym.name, t_type)
            if not target_sym.name and t_type == 'STT_SECTION':
                offset_within = t_off - target_func['start']
                addend_str = ""

                if offset_within:
                    addend_str = f"+{hex(offset_within)}"

                base_str = format_sym(target_func['name'], target_func['type'])
                t_str = f"{base_str}{addend_str}"

            if reference_name.startswith('.text'):
                hint_macro = "CODE_REFERENCES_INIT_DATA"
            elif reference_name.startswith('.rodata'):
                hint_macro = "RODATA_REFERENCES_INIT_DATA"
            elif reference_name.startswith('.data'):
                hint_macro = "DATA_REFERENCES_INIT_DATA"
            else:
                if call_info['type'] == 'STT_FUNC':
                    hint_macro = "CODE_REFERENCES_INIT_DATA"
                else:
                    hint_macro = "DATA_REFERENCES_INIT_DATA"

            print("❌ Illegal free-after-init data reference: "
                  f"{call_str} -> {t_str}")
            print(f"Mark the reference as '{hint_macro}' if this "
                  "is intentional\n")
            illegal_refs_found += 1

    return illegal_refs_found


def main() -> None:
    desc = "Analyzes ELF object files for illegal free-after-init references"
    parser = argparse.ArgumentParser(description=desc)

    parser.add_argument(
        "obj_file",
        type=str,
        help="Path to an ELF object file to analyze"
    )

    args = parser.parse_args()

    with open(args.obj_file, 'rb') as f:
        elf = ELFFile(f)
        num_illegal_refs = find_illegal_free_after_init_references(elf)

    if num_illegal_refs != 0:
        sys.exit(1)

    print("✅ No illegal free-after-init references found!")


if __name__ == '__main__':
    main()
