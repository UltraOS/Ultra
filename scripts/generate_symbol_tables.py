#!/usr/bin/python3
#
# Generate Linux-compatible kallsyms symbol tables
#
# Token interning is using the same algorithm as the original script, albeit
# written from scratch to be more pythonic. Optional linux compatibility is
# available to ensure script correctness.
#

import argparse
import subprocess
import functools
from dataclasses import dataclass
from typing import List, Tuple, Optional
from types import TracebackType
from enum import Enum
from abc import ABC, abstractmethod


class Mode(Enum):
    # Ultra mode, C backend, no symbol quirks
    ULTRA = 1

    # Linux mode, GAS backend, special symbol heuristics
    # (produces data identical to scripts/kallsyms.c)
    LINUX = 2


LEB128_HAS_NEXT_BYTE = 1 << 7
LEB128_BITS_PER_BYTE = 7


def leb128_max(byte_width: int) -> int:
    return (1 << (byte_width * LEB128_BITS_PER_BYTE)) - 1


def to_uleb128(number: int, byte_idx: int) -> int:
    return (number >> (byte_idx * LEB128_BITS_PER_BYTE)) & leb128_max(1)


class Symbol(ABC):
    def __init__(self, name: str, type: str, index: int, address: int) -> None:
        self._name = name
        self._type = type
        self._index = index
        self._address = address

    @property
    def name(self) -> str:
        return self._name

    @property
    def type(self) -> str:
        return self._type

    @property
    def index(self) -> int:
        return self._index

    @index.setter
    def index(self, value: int) -> None:
        self._index = value

    @property
    def address(self) -> int:
        return self._address

    def is_weak(self) -> bool:
        # w is not considered as it's filtered out earlier
        return self._type == "W"

    @abstractmethod
    def canonical_name(self) -> str:
        pass

    @abstractmethod
    def is_linker(self) -> bool:
        pass

    @staticmethod
    def sort_by_address_comparator(lhs: 'Symbol', rhs: 'Symbol') -> int:
        if lhs.address != rhs.address:
            if lhs.address < rhs.address:
                return -1
            return 1

        # Put weak symbols after non-weak symbols
        # (same reason as for the linker symbols below)
        if lhs.is_weak() != rhs.is_weak():
            return lhs.is_weak() - rhs.is_weak()

        # Linker symbols might be sorted _after_ non-linker symbols, otherwise
        # functions which appear at the start/end of the section might be
        # replaced by a nearby linker symbol in the backtrace.
        if lhs.is_linker() != rhs.is_linker():
            return lhs.is_linker() - rhs.is_linker()

        # Linux also sorts by the number of underscores
        # We technically don't need this logic because we don't prefix stuff
        # with underscores, but let's keep it anyway so we don't have to split
        # this into a per-mode function
        lhs_underscores = len(lhs.name) - len(lhs.name.lstrip("_"))
        rhs_underscores = len(rhs.name) - len(rhs.name.lstrip("_"))

        if lhs_underscores != rhs_underscores:
            return lhs_underscores - rhs_underscores

        return lhs.index - rhs.index

    @staticmethod
    def sort_by_name_comparator(lhs: 'Symbol', rhs: 'Symbol') -> int:
        if lhs.name < rhs.name:
            return -1
        if rhs.name < lhs.name:
            return 1

        if lhs.address != rhs.address:
            if lhs.address < rhs.address:
                return -1
            return 1

        return lhs.index - rhs.index


class LinuxSymbol(Symbol):
    def __init__(self, name: str, type: str, index: int, address: int) -> None:
        super().__init__(name, type, index, address)

    def canonical_name(self) -> str:
        # Linux stores the type of the symbol as the first byte of the name
        return self._type + self._name

    def is_linker(self) -> bool:
        if len(self._name) < 8:
            return False

        if not self._name.startswith("__"):
            return False

        return any(self._name.startswith(x) for x in [
            "__start_",
            "__stop_",
            "__end_",
        ]) or any(self._name.endswith(x) for x in [
            "_start",
            "_end",
        ])


class UltraSymbol(Symbol):
    def __init__(self, name: str, type: str, index: int, address: int) -> None:
        super().__init__(name, type, index, address)

    def canonical_name(self) -> str:
        return self.name

    def is_linker(self):
        return self.name.startswith(UltraContext.LINKER_SYMBOL_PREFIX)


@dataclass
class Section:
    start_name: str
    end_name: str
    begin: int
    end: int

    def contains(self, addr: int) -> bool:
        return addr >= self.begin and addr <= self.end


class Context(ABC):
    MAX_SYMBOL_LENGTH: int

    def __init__(
        self, text_start_name: str, text_end_name: str,
        binary_path: Optional[str]
    ) -> None:
        self._text = Section(text_start_name, text_end_name, 0, 0)
        self.__symbols: List[Symbol] = []

        if binary_path:
            nm_output = subprocess.check_output(
                ["nm", "-n", binary_path], universal_newlines=True
            ).splitlines()

            raw_symbols = [e.split(" ") for e in nm_output]
            for sym in raw_symbols:
                assert len(sym) == 3
                self.__add_symbol(
                    sym[2], sym[1], len(self.__symbols), int(sym[0], 16)
                )

            self.__symbols = list(filter(
                lambda sym: not self._should_drop(sym), self.__symbols
            ))
            self.__symbols.sort(
                key=functools.cmp_to_key(Symbol.sort_by_address_comparator)
            )

    @property
    def symbols(self) -> List[Symbol]:
        return self.__symbols

    def start_of_text(self) -> int:
        return self._text.begin

    def _should_drop(self, symbol: Symbol) -> bool:
        return not self._text.contains(symbol.address)

    @staticmethod
    @abstractmethod
    def make_symbol(name: str, type: str, index: int, address: int) -> Symbol:
        pass

    @staticmethod
    @abstractmethod
    def make_table_generator(out_path: str) -> 'TableGenerator':
        pass

    def _on_symbol_added(self, symbol: Symbol) -> None:
        if symbol.name == self._text.start_name:
            self._text.begin = symbol.address
            return

        if symbol.name == self._text.end_name:
            self._text.end = symbol.address
            return

    def __add_symbol(
        self, name: str, type: str, index: int, address: int
    ) -> None:
        # Explicitly skip the following:
        # - Undefined symbol: U
        # - Absolute local and global: a, A
        # - Debugging symbol: N
        # The rest of the garbage should be skipped when we filter by the .text
        # section bounds
        if type in "UaAN":
            return

        # Skip ARM "mapping symbols"
        if name.startswith(("$a.", "$t.", "$d.", "$x.")):
            return

        if len(name) > self.MAX_SYMBOL_LENGTH:
            raise RuntimeError(
                f'Symbol "{name}" is too long, adjust MAX_SYMBOL_LENGTH'
            )

        symbol = self.make_symbol(name, type, index, address)

        self.__symbols.append(symbol)
        self._on_symbol_added(symbol)

    def sort_by_name(self) -> None:
        self.__symbols.sort(
            key=functools.cmp_to_key(Symbol.sort_by_name_comparator)
        )


class LinuxContext(Context):
    MAX_SYMBOL_LENGTH = 511

    def __init__(self, binary_path: Optional[str]):
        self.__init_text = Section("_sinittext", "_einittext", 0, 0)
        super().__init__("_stext", "_etext", binary_path)

    @staticmethod
    def make_symbol(name: str, type: str, index: int, address: int) -> Symbol:
        return LinuxSymbol(name, type, index, address)

    @staticmethod
    def make_table_generator(out_path: str) -> 'TableGenerator':
        return GASGenerator(out_path)

    def _on_symbol_added(self, symbol: Symbol) -> None:
        super()._on_symbol_added(symbol)

        if symbol.name == self.__init_text.start_name:
            self.__init_text.begin = symbol.address
            return

        if symbol.name == self.__init_text.end_name:
            self.__init_text.end = symbol.address
            return

    def _should_drop(self, symbol):
        if symbol.name.startswith(("__start_", "__stop_")):
            return False

        if (symbol.address == self._text.end and
           symbol.name != self._text.end_name):
            return True

        if (symbol.address == self.__init_text.end and
           symbol.name != self.__init_text.end_name):
            return True

        if self.__init_text.contains(symbol.address):
            return False

        return super()._should_drop(symbol)


class UltraContext(Context):
    MAX_SYMBOL_LENGTH = 127
    LINKER_SYMBOL_PREFIX: str = "g_linker_symbol_"

    @staticmethod
    def text_begin_symbol() -> str:
        return UltraContext.LINKER_SYMBOL_PREFIX + "text_begin"

    @staticmethod
    def text_end_symbol() -> str:
        return UltraContext.LINKER_SYMBOL_PREFIX + "text_end"

    @staticmethod
    def make_symbol(name: str, type: str, index: int, address: int) -> Symbol:
        return UltraSymbol(name, type, index, address)

    @staticmethod
    def make_table_generator(out_path: str) -> 'TableGenerator':
        return CGenerator(out_path)

    def _should_drop(self, symbol):
        # We don't need linker symbols since they can't appear in traces
        if symbol.name.startswith(UltraContext.LINKER_SYMBOL_PREFIX):
            return True
        return super()._should_drop(symbol)

    def __init__(self, binary_path: Optional[str]):
        super().__init__(
            UltraContext.text_begin_symbol(), UltraContext.text_end_symbol(),
            binary_path
        )


def make_context(mode: Mode, binary_path: Optional[str]) -> Context:
    return {
        Mode.LINUX: LinuxContext,
        Mode.ULTRA: UltraContext,
    }[mode](binary_path)


class TokenTable:
    class TokenizedSymbol:
        def __init__(self, symbol: Symbol):
            self.tokens: List[int] = [ord(c) for c in symbol.canonical_name()]

    def __init__(self, symbols: List[Symbol]) -> None:
        self.symbols = [TokenTable.TokenizedSymbol(s) for s in symbols]

        # This stores two-byte sequences and we want to have O(1) lookups
        self.multibyte_tokens: List[int] = [0] * 0x10000  # (UINT16_MAX + 1)

        # Top 256 tokens that will end up in kallsyms
        #
        # These are recursive:
        # If the token at index only has 1 entry this is the token
        # Otherwise it's a compound token consisting of two sub-tokens
        self.best_tokens: List[List[int]] = [[]] * 0x100  # (UINT8_MAX + 1)

        for symbol in self.symbols:
            self.__add_symbol(symbol, record_for_best=True)

        self.__optimize()

    def __for_each_token(self, symbol: TokenizedSymbol, addend: int,
                         record_for_best: bool = False) -> None:
        for i in range(0, len(symbol.tokens) - 1):
            self.multibyte_tokens[
                (symbol.tokens[i + 1] << 8) + symbol.tokens[i]
            ] += addend

        if record_for_best:
            for token in symbol.tokens:
                self.best_tokens[token] = [token]

    def __add_symbol(
        self, symbol: TokenizedSymbol, record_for_best: bool = False
    ) -> None:
        self.__for_each_token(symbol, +1, record_for_best)

    def __remove_symbol(self, symbol: TokenizedSymbol) -> None:
        self.__for_each_token(symbol, -1)

    def __most_used_multibyte_token(self) -> Tuple[int, int]:
        best_score = -1
        best_index = 0

        for i, score in enumerate(self.multibyte_tokens):
            if score <= best_score:
                continue

            best_score = score
            best_index = i

        return (best_index, best_score)

    def __compress_with_compound_token(self, idx: int) -> None:
        tgt_token = self.best_tokens[idx]

        for symbol in self.symbols:
            i: int = 0
            did_compress = False

            tokens = symbol.tokens
            sym_len = len(tokens)

            while i < (sym_len - 1):
                if tokens[i] != tgt_token[0] or tokens[i + 1] != tgt_token[1]:
                    i += 1
                    continue

                if not did_compress:
                    self.__remove_symbol(symbol)
                    did_compress = True

                symbol.tokens[i:i + 2] = [idx]
                sym_len -= 1

            if did_compress:
                self.__add_symbol(symbol)

    def __optimize(self) -> None:
        for i, this_token in reversed(list(enumerate(self.best_tokens))):
            if this_token:
                continue

            most_used = self.__most_used_multibyte_token()
            if most_used[1] == 0:
                # Nothing to optimize, no tokens are used
                break

            self.best_tokens[i] = [
                (most_used[0] >> 0) & 0xFF,
                (most_used[0] >> 8) & 0xFF
            ]

            self.__compress_with_compound_token(i)

    def unwind_compound(self, tokens: List[int]) -> str:
        out_str = ""

        for token in tokens:
            if len(self.best_tokens[token]) == 1:
                out_str += chr(token)
            else:  # compound token
                out_str += self.unwind_compound(self.best_tokens[token])

        return out_str


class ValueType(Enum):
    U8 = 1
    U8_ARRAY = 2
    U16 = 3
    U16_ARRAY = 4
    U32 = 5
    ASCII_STRING = 6


@dataclass
class Value:
    data: int | str | List[int]
    type: ValueType

    @staticmethod
    def u16(value: int) -> 'Value':
        assert value <= 0xFFFF
        return Value(value, ValueType.U16)

    @staticmethod
    def u32(value: int) -> 'Value':
        assert value <= 0xFFFFFFFF
        return Value(value, ValueType.U32)

    @staticmethod
    def u8_array(value: List[int]) -> 'Value':
        return Value(value, ValueType.U8_ARRAY)

    @staticmethod
    def u16_array(value: List[int]) -> 'Value':
        return Value(value, ValueType.U16_ARRAY)

    @staticmethod
    def ascii_string(value: str) -> 'Value':
        return Value(value, ValueType.ASCII_STRING)


class TableType(Enum):
    SYMBOL_COUNT = 0
    SYMBOL_NAMES = 1
    SYMBOL_MARKERS = 2
    SYMBOL_ADDRESSES = 3
    SYMBOL_BASE = 4
    SYMBOL_INDICES = 5
    TOKEN_TABLE = 6
    TOKEN_OFFSETS = 7


class TableGenerator(ABC):
    def __init__(self, path: str) -> None:
        self._file = open(path, "w")

    def __enter__(self) -> 'TableGenerator':
        return self

    def __exit__(
        self, exc_type: Optional[type[BaseException]],
        ex: Optional[BaseException], traceback: Optional[TracebackType]
    ) -> Optional[bool]:
        self._file.close()
        return None

    def _write(self, data: str) -> None:
        self._file.write(data)

    class ArrayEmitter(ABC):
        def __init__(
            self, parent: 'TableGenerator', table_type: TableType,
            array_type: ValueType
        ) -> None:
            self._parent = parent
            self._table_type = table_type
            self._array_type = array_type

        @abstractmethod
        def __enter__(self):
            pass

        @abstractmethod
        def __exit__(
            self, exc_type: Optional[type[BaseException]],
            ex: Optional[BaseException], traceback: Optional[TracebackType]
        ) -> Optional[bool]:
            pass

        @abstractmethod
        def emit(
            self, value: Value, comment: Optional[str] = None
        ) -> None:
            pass

    @abstractmethod
    def array(self, table_type: TableType, type: ValueType) -> ArrayEmitter:
        pass

    @abstractmethod
    def emit(self, table_type: TableType, value: Value) -> None:
        pass


class CGenerator(TableGenerator):
    def __init__(self, path: str) -> None:
        super().__init__(path)

        self._write("#include <common/types.h>\n")
        self._write("#include <symbols.h>\n\n")
        self._write("#include <private/symbols.h>\n\n")

    @staticmethod
    def __type_to_string(type: ValueType) -> str:
        return "const " + {
            ValueType.U8: "u8",
            ValueType.U8_ARRAY: "u8",
            ValueType.U16: "u16",
            ValueType.U16_ARRAY: "u16",
            ValueType.U32: "u32",
            ValueType.ASCII_STRING: "char",
        }[type]

    @staticmethod
    def __table_type_to_string(table_type: TableType) -> str:
        return "g_symbol_" + {
            TableType.SYMBOL_COUNT: "count",
            TableType.SYMBOL_NAMES: "compressed_names",
            TableType.SYMBOL_MARKERS: "name_offsets",
            TableType.SYMBOL_ADDRESSES: "relative_addresses",
            TableType.SYMBOL_BASE: "base",
            TableType.SYMBOL_INDICES: "name_index_to_address_index",
            TableType.TOKEN_TABLE: "token_table",
            TableType.TOKEN_OFFSETS: "token_offsets",
        }[table_type]

    def _emit_label(
        self, table_type: TableType, value_type: ValueType
    ) -> None:
        self._write(CGenerator.__type_to_string(value_type))
        self._write(" ")
        self._write(CGenerator.__table_type_to_string(table_type))

    def _emit_value(self, value: Value) -> None:
        if value.type in (ValueType.U8, ValueType.U16, ValueType.U32):
            self._write(str(value.data))
            return

        if value.type in (ValueType.U8_ARRAY, ValueType.U16_ARRAY):
            assert isinstance(value.data, list)
            hex_width = 2 if value.type == ValueType.U8_ARRAY else 4
            arr = ", ".join([f"0x{val:0{hex_width}x}" for val in value.data])

            self._write(arr)
            return

        if value.type == ValueType.ASCII_STRING:
            assert isinstance(value.data, str)
            as_chars = [f"'{char}'" for char in value.data]
            as_chars.append("'\\0'")
            self._write(", ".join(as_chars))
            return

        raise RuntimeError(f"Bad ValueType: {value.type}")

    def emit(self, table_type: TableType, value: Value) -> None:
        assert value.type not in (ValueType.U8_ARRAY, ValueType.U16_ARRAY)

        if table_type == TableType.SYMBOL_BASE:
            self._write("const ptr_t ")
            self._write(CGenerator.__table_type_to_string(table_type))
        else:
            self._emit_label(table_type, value.type)

        self._write(" = ")
        self._emit_value(value)

        if table_type == TableType.SYMBOL_BASE:
            self._write(" + (ptr_t)g_linker_symbol_text_begin")

        self._write(";\n\n")

    class ArrayEmitter(TableGenerator.ArrayEmitter):
        def __init__(
            self, parent: 'TableGenerator', table_type: TableType,
            type: ValueType
        ) -> None:
            super().__init__(parent, table_type, type)

        def __enter__(self):
            self._parent._emit_label(self._table_type, self._array_type)
            self._parent._write("[] = {\n")
            return self

        def __exit__(
            self, exc_type: Optional[type[BaseException]],
            ex: Optional[BaseException], traceback: Optional[TracebackType]
        ) -> Optional[bool]:
            self._parent._write("};\n\n")
            return None

        def emit(
            self, value: Value, comment: Optional[str] = None
        ) -> None:
            assert isinstance(self._parent, CGenerator)
            self._parent._write("    ")
            self._parent._emit_value(value)
            self._parent._write(",")

            if comment:
                self._parent._write(f" /* {comment} */\n")
            else:
                self._parent._write("\n")

    def array(self, table_type: TableType, type: ValueType) -> ArrayEmitter:
        return CGenerator.ArrayEmitter(self, table_type, type)


class GASGenerator(TableGenerator):
    def __init__(self, path: str) -> None:
        super().__init__(path)

        self._write("#include <asm/bitsperlong.h>\n")
        self._write("#if BITS_PER_LONG == 64\n")
        self._write("#define PTR .quad\n")
        self._write("#define ALGN .balign 8\n")
        self._write("#else\n")
        self._write("#define PTR .long\n")
        self._write("#define ALGN .balign 4\n")
        self._write("#endif\n")
        self._write("\t.section .rodata, \"a\"\n")

    def _emit_label(self, table_type: TableType) -> None:
        name = "kallsyms_" + {
            TableType.SYMBOL_COUNT: "num_syms",
            TableType.SYMBOL_NAMES: "names",
            TableType.SYMBOL_MARKERS: "markers",
            TableType.SYMBOL_ADDRESSES: "offsets",
            TableType.SYMBOL_BASE: "relative_base",
            TableType.SYMBOL_INDICES: "seqs_of_names",
            TableType.TOKEN_TABLE: "token_table",
            TableType.TOKEN_OFFSETS: "token_index",
        }[table_type]

        self._write(f".globl {name}\n")
        self._write("\tALGN\n")
        self._write(f"{name}:\n")

    def _emit_comment_or_newline(self, comment: Optional[str]) -> None:
        if comment:
            self._write(f"\t/* {comment} */\n")
        else:
            self._write("\n")

    @staticmethod
    def _make_gas_prologue(table_type: TableType, value: Value) -> str:
        string_type = {
            ValueType.U8: "byte",
            ValueType.U8_ARRAY: "byte",
            ValueType.U16: "short",
            ValueType.U16_ARRAY: "short",
            ValueType.U32: "long",
            ValueType.ASCII_STRING: "asciz",
        }[value.type]

        prefix = " " if table_type in (
            TableType.SYMBOL_NAMES, TableType.SYMBOL_INDICES
        ) else "\t"

        return f".{string_type}{prefix}"

    @staticmethod
    def _alternate_form_hex_fmt(value: Value) -> str:
        # Python doesn't handle the alternate form ("#") for hex numbers
        # which are 0 correctly (it unconditionally prefixes them with 0x)
        # so handle the zero case manually
        if not value.data:
            return "0"

        return f"0x{value.data:x}"

    def emit(self, table_type: TableType, value: Value) -> None:
        self._emit_label(table_type)

        if table_type == TableType.SYMBOL_BASE:
            assert value.type == ValueType.U32

            offset = GASGenerator._alternate_form_hex_fmt(value)
            self._write(f"\tPTR\t_text + {offset}\n\n")
            return

        prologue = GASGenerator._make_gas_prologue(table_type, value)
        self._write(f"\t{prologue}{value.data}\n\n")

    class ArrayEmitter(TableGenerator.ArrayEmitter):
        def __init__(
            self, parent: 'TableGenerator', table_type: TableType,
            type: ValueType
        ) -> None:
            super().__init__(parent, table_type, type)

        def __enter__(self):
            self._parent._emit_label(self._table_type)
            return self

        def __exit__(
            self, exc_type: Optional[type[BaseException]],
            ex: Optional[BaseException], traceback: Optional[TracebackType]
        ) -> Optional[bool]:
            self._parent._write("\n")
            return None

        def __hex_format(self):
            return self._table_type == TableType.SYMBOL_ADDRESSES

        def emit(
            self, value: Value, comment: Optional[str] = None
        ) -> None:
            prologue = GASGenerator._make_gas_prologue(self._table_type, value)
            self._parent._write(f"\t{prologue}")

            if value.type in (ValueType.U8, ValueType.U16, ValueType.U32):
                if self.__hex_format():
                    data = GASGenerator._alternate_form_hex_fmt(value)
                else:
                    data = str(value.data)

                self._parent._write(data)
            elif value.type in (ValueType.U8_ARRAY, ValueType.U16_ARRAY):
                assert isinstance(value.data, list)
                hex_width = 2 if value.type == ValueType.U8_ARRAY else 4
                self._parent._write(
                    ", ".join([f"0x{val:0{hex_width}x}" for val in value.data])
                )
            elif value.type == ValueType.ASCII_STRING:
                self._parent._write(f"\"{value.data}\"")
            else:
                raise RuntimeError("Bad ValueType " + str(value.type))

            assert isinstance(self._parent, GASGenerator)
            self._parent._emit_comment_or_newline(comment)

    def array(self, table_type: TableType, type: ValueType) -> ArrayEmitter:
        return GASGenerator.ArrayEmitter(self, table_type, type)


def main() -> None:
    parser = argparse.ArgumentParser("Generate the kernel symbol tables")
    parser.add_argument("out_file", help="Target to output the symbol tables")
    parser.add_argument("--binary",
                        help="Path to the kernel binary, produces empty "
                             "symbol tables if omitted")
    parser.add_argument("--linux-mode", action="store_true",
                        help="Enables GAS backend & linux-specific symbols"
                             " (fully compatible with kallsyms)")
    args = parser.parse_args()

    ctx = make_context(
        Mode.LINUX if args.linux_mode else Mode.ULTRA,
        args.binary
    )
    token_table = TokenTable(ctx.symbols)

    byte_offset = 0
    offset_markers: List[int] = []

    with ctx.make_table_generator(args.out_file) as gen:
        gen.emit(TableType.SYMBOL_COUNT, Value.u32(len(ctx.symbols)))

        with gen.array(TableType.SYMBOL_NAMES, ValueType.U8_ARRAY) as arr:
            for idx, tokenized_symbol in enumerate(token_table.symbols):
                if (idx & 0xFF) == 0:
                    offset_markers.append(byte_offset)

                ctx.symbols[idx].index = idx

                name_len = len(tokenized_symbol.tokens)
                assert tokenized_symbol.tokens and name_len <= leb128_max(2)

                repr: List[int] = []

                if name_len <= leb128_max(1):
                    repr.append(name_len)
                    byte_offset += name_len + 1
                else:
                    repr.append(LEB128_HAS_NEXT_BYTE | to_uleb128(name_len, 0))
                    repr.append(to_uleb128(name_len, 1))
                    byte_offset += name_len + 2

                repr.extend(tokenized_symbol.tokens)
                arr.emit(
                    Value(repr, ValueType.U8_ARRAY),
                    comment=ctx.symbols[idx].canonical_name()
                )

        with gen.array(TableType.SYMBOL_MARKERS, ValueType.U32) as arr:
            for marker in offset_markers:
                arr.emit(Value.u32(marker))

        byte_offset = 0
        offset_markers.clear()

        with gen.array(TableType.TOKEN_TABLE, ValueType.ASCII_STRING) as arr:
            for token in token_table.best_tokens:
                offset_markers.append(byte_offset)
                unwound_token = token_table.unwind_compound(token)

                arr.emit(Value.ascii_string(unwound_token))
                byte_offset += len(unwound_token) + 1

        with gen.array(TableType.TOKEN_OFFSETS, ValueType.U16) as arr:
            for marker in offset_markers:
                arr.emit(Value.u16(marker))

        first_symbol_address = ctx.symbols[0].address if ctx.symbols else 0

        with gen.array(TableType.SYMBOL_ADDRESSES, ValueType.U32) as arr:
            for symbol in ctx.symbols:
                arr.emit(
                    Value.u32(symbol.address - first_symbol_address),
                    comment=symbol.canonical_name()
                )

        first_symbol_offset = first_symbol_address - ctx.start_of_text()
        gen.emit(TableType.SYMBOL_BASE, Value.u32(first_symbol_offset))

        ctx.sort_by_name()

        with gen.array(TableType.SYMBOL_INDICES, ValueType.U8_ARRAY) as arr:
            for symbol in ctx.symbols:
                indices = [
                    (symbol.index >> 16) & 0xFF,
                    (symbol.index >> 8) & 0xFF,
                    (symbol.index >> 0) & 0xFF,
                ]
                arr.emit(
                    Value.u8_array(indices), comment=symbol.canonical_name()
                )


if __name__ == "__main__":
    main()
