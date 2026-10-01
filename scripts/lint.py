#!/usr/bin/python3
import argparse
import functools
import json
import os
import re
import shlex
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from typing import Any, Callable, Dict, List, NamedTuple, Optional, Set, Tuple


class Finding(NamedTuple):
    path: str
    line: int
    message: str


class Context(NamedTuple):
    root: str
    build_dir: str


Check = Callable[[Context], List[Finding]]

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

THIRD_PARTY = ("/uacpi/", "/flanterm/", "/boot/ultra_protocol.h")
GENERATED_SOURCES = ("kernel_symbols", "build_banner")
SOURCE_SUFFIXES = (".c", ".h", ".S", ".cpp", ".hpp")

C_TOKEN = re.compile(
    r"""
    (?P<line_comment>//[^\n]*)
  | (?P<block_comment>/\*.*?\*/)
  | (?P<string>"(?:\\.|[^"\\\n])*")
  | (?P<char>'(?:\\.|[^'\\\n])*')
  | (?P<null>\bNULL\b)
    """,
    re.S | re.X
)


def is_third_party(path: str) -> bool:
    return any(part in "/" + path for part in THIRD_PARTY)


@functools.lru_cache(maxsize=None)
def tracked_sources(ctx: Context) -> List[str]:
    out = subprocess.check_output(
        ["git", "ls-files"], cwd=ctx.root, text=True
    )
    return [
        path for path in out.splitlines()
        if path.endswith(SOURCE_SUFFIXES) and not is_third_party(path)
    ]


@functools.lru_cache(maxsize=None)
def read_source(ctx: Context, path: str) -> str:
    with open(os.path.join(ctx.root, path), errors="replace") as f:
        return f.read()


def line_of(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def check_null(ctx: Context) -> List[Finding]:
    findings = []

    for path in tracked_sources(ctx):
        text = read_source(ctx, path)

        for match in C_TOKEN.finditer(text):
            if match.lastgroup != "null":
                continue

            line = line_of(text, match.start())
            findings.append(Finding(path, line, "use nullptr, not NULL"))

    return findings


def check_comments(ctx: Context) -> List[Finding]:
    findings = []

    for path in tracked_sources(ctx):
        text = read_source(ctx, path)
        previous_line_comment = -1

        for match in C_TOKEN.finditer(text):
            comment = match.group()
            line = line_of(text, match.start())

            if match.lastgroup == "line_comment":
                line_start = text.rfind("\n", 0, match.start()) + 1
                if text[line_start:match.start()].strip():
                    continue

                if line == previous_line_comment + 1:
                    findings.append(Finding(
                        path, line - 1, "use /* */ for a multi-line comment"
                    ))
                previous_line_comment = line
                continue

            if match.lastgroup != "block_comment":
                continue

            if "\n" not in comment:
                rest = text[match.end():].split("\n", 1)[0]
                if not rest.rstrip().endswith("\\"):
                    findings.append(Finding(
                        path, line, "use // for a single line comment"
                    ))
                continue

            column = match.start() - text.rfind("\n", 0, match.start()) - 1
            lead = " " * (column + 1) + "*"

            for offset, body in enumerate(comment.split("\n")[1:], 1):
                if body.startswith(lead) and not body.startswith(lead + "*"):
                    continue

                findings.append(Finding(
                    path, line + offset,
                    "align the * of every comment line under the * of /*"
                ))

    return findings


INCLUDE = re.compile(r'^\s*#\s*include\s*(?P<open>[<"])(?P<path>[^>"]*)')


def check_includes(ctx: Context) -> List[Finding]:
    findings = []

    for path in tracked_sources(ctx):
        text = read_source(ctx, path)

        for number, line in enumerate(text.split("\n"), 1):
            match = INCLUDE.match(line)
            if match is None:
                continue

            if match.group("open") == '"':
                findings.append(Finding(
                    path, number, "include with <>"
                ))
            elif any(part in (".", "..") for part in
                     match.group("path").split("/")):
                findings.append(Finding(
                    path, number, "include by the path from an include root"
                ))

    return findings


def check_tabs(ctx: Context) -> List[Finding]:
    findings = []

    for path in tracked_sources(ctx):
        text = read_source(ctx, path)

        for number, line in enumerate(text.split("\n"), 1):
            if "\t" in line:
                findings.append(Finding(path, number, "indent with spaces"))

    return findings


def check_file_end(ctx: Context) -> List[Finding]:
    findings = []

    for path in tracked_sources(ctx):
        text = read_source(ctx, path)
        last = text.count("\n")

        if not text.endswith("\n"):
            findings.append(Finding(
                path, last + 1, "add a newline at the end"
            ))
        elif text.endswith("\n\n"):
            findings.append(Finding(
                path, last, "drop the blank line at the end"
            ))

    return findings


def compiler_command(entry: Dict[str, Any]) -> Optional[List[str]]:
    if "command" in entry:
        args = shlex.split(entry["command"])
    else:
        args = list(entry["arguments"])

    while args and not os.path.basename(args[0]).startswith("clang"):
        args = args[1:]
    if not args:
        return None

    command = [args[0]]
    skip_next = False

    for arg in args[1:]:
        if skip_next:
            skip_next = False
        elif arg in ("-o", "-MF", "-MT", "-MQ"):
            skip_next = True
        elif arg not in ("-c", "-MD", "-MMD"):
            command.append(arg)

    return command + ["-fsyntax-only", "-w", "-Xclang", "-ast-dump=json"]


Variable = Tuple[Optional[str], int, str, str]
Located = Tuple[Optional[str], int, str]


class ScanResult(NamedTuple):
    variables: List[Variable]
    enums: List[Located]
    formats: List[Located]


FORMAT_SPEC = re.compile(
    r"%(?P<flags>[-+ 0#]*)(?P<width>\*|\d+)?(?:\.(?P<precision>\*|\d+))?"
    r"(?P<length>hh|h|ll|l|z|j|t|L|q)?(?P<conversion>.)"
)
SUPPORTED_LENGTHS = ("", "hh", "h", "ll", "l", "z")
SUPPORTED_CONVERSIONS = "cspdiouxX"

PRINTF_SOURCE = "kernel/common/format.c"

POINTER_EXTENSIONS = ("RSM", "SM", "S", "E", "PCI", "PA", "V")
POINTER_EXTENSION_TYPES = {
    "PA": ("phys_addr_t",),
    "E": ("error_t",),
    "S": ("struct string",),
    "PCI": ("struct pci_address",),
    "V": ("struct nested_printf",),
}

SIGNED = "di"
UNSIGNED = "uxXo"
SHORT_LENGTHS = ("", "h", "hh")


class Rule(NamedTuple):
    lengths: Tuple[str, ...]
    conversions: str
    expected: str


INT_RULE = Rule(SHORT_LENGTHS, SIGNED + "c", "%d")
UINT_RULE = Rule(SHORT_LENGTHS, UNSIGNED, "%u or %X")
CHAR_RULE = Rule(("",), "cd", "%c or %d")
U64_RULE = Rule(("ll",), UNSIGNED, "%llu or %llX")
I64_RULE = Rule(("ll",), SIGNED, "%lld")
SIZE_RULE = Rule(("z",), UNSIGNED, "%zu or %zX")
SSIZE_RULE = Rule(("z",), SIGNED, "%zd")
STRING_RULE = Rule(("",), "sp", "%s")
PHYS_RULE = Rule((), "", "%pPA with its address")
ERROR_RULE = Rule((), "", "%pE with its address")

TYPEDEF_RULES = {
    "phys_addr_t": PHYS_RULE,
    "error_t": ERROR_RULE,
    "size_t": SIZE_RULE,
    "ptr_t": SIZE_RULE,
    "reg_t": SIZE_RULE,
    "virt_addr_t": SIZE_RULE,
    "ssize_t": SSIZE_RULE,
    "u64": U64_RULE,
    "i64": I64_RULE,
    "u32": UINT_RULE,
    "u16": UINT_RULE,
    "u8": UINT_RULE,
    "i32": INT_RULE,
    "i16": INT_RULE,
    "i8": INT_RULE,
    "bool": INT_RULE,
}

BUILTIN_RULES = {
    "_Bool": INT_RULE,
    "char": CHAR_RULE,
    "signed char": INT_RULE,
    "short": INT_RULE,
    "int": INT_RULE,
    "unsigned char": UINT_RULE,
    "unsigned short": UINT_RULE,
    "unsigned int": UINT_RULE,
    "long": SSIZE_RULE,
    "unsigned long": SIZE_RULE,
    "long long": I64_RULE,
    "unsigned long long": U64_RULE,
}

CHAR_STRING = re.compile(r"^char( \*|\[\d*\])$")
COMPUTED = ("BinaryOperator", "ConditionalOperator")
COMPUTING_UNARY = ("-", "+", "~", "!")


def bare_type(name: str) -> str:
    for qualifier in ("const ", "volatile "):
        name = name.replace(qualifier, "")
    return name.strip()


def strip_implicit(node: Dict[str, Any]) -> Dict[str, Any]:
    while node.get("kind") in ("ImplicitCastExpr", "ParenExpr"):
        node = node["inner"][0]
    return node


def node_types(node: Dict[str, Any]) -> Tuple[str, str]:
    qual_type = node.get("type", {})
    sugared = bare_type(qual_type.get("qualType", ""))
    plain = bare_type(qual_type.get("desugaredQualType", sugared))
    return sugared, plain


def is_computed(node: Dict[str, Any]) -> bool:
    if node.get("kind") == "UnaryOperator":
        return node.get("opcode") in COMPUTING_UNARY

    return node.get("kind") in COMPUTED


def mentions_type(node: Dict[str, Any], name: str) -> bool:
    if node.get("kind") == "CStyleCastExpr":
        return False
    if (node.get("kind") in ("DeclRefExpr", "MemberExpr") and
            node_types(node)[0] == name):
        return True

    return any(
        mentions_type(child, name) for child in node.get("inner", [])
        if isinstance(child, dict)
    )


class AstScanner:
    def __init__(self) -> None:
        self.current_file: Optional[str] = None
        self.current_line = 0
        self.variables: List[Variable] = []
        self.enums: List[Located] = []
        self.formats: List[Located] = []
        self.format_functions: Set[str] = set()
        self.in_printf_source = False
        self.enum_types: Dict[str, Tuple[str, str]] = {}

    # clang omits the file and the line of a location when they repeat the last
    # ones it printed, in document order, so both are carried along here
    def note_location(self, loc: Any) -> int:
        if not isinstance(loc, dict):
            return self.current_line

        if "expansionLoc" in loc:
            self.note_location(loc.get("spellingLoc"))
            return self.note_location(loc["expansionLoc"])

        if "file" in loc:
            self.current_file = loc["file"]
        if "line" in loc:
            self.current_line = loc["line"]
        return self.current_line

    def walk(self, node: Dict[str, Any], in_function: bool) -> None:
        line = self.note_location(node.get("loc"))
        where = self.current_file

        extent = node.get("range", {})
        call_line = self.note_location(extent.get("begin"))
        call_where = self.current_file
        self.note_location(extent.get("end"))

        kind = node.get("kind")
        if kind == "VarDecl" and "name" in node:
            storage = node.get("storageClass")

            if storage == "static":
                self.variables.append((where, line, node["name"], "s_"))
            elif not in_function and storage in (None, "extern"):
                self.variables.append((where, line, node["name"], "g_"))

        inner = node.get("inner", [])
        if kind == "FunctionDecl" and any(
            child.get("kind") == "FormatAttr" for child in inner
        ):
            self.format_functions.add(node.get("name", ""))

        if kind == "EnumDecl" and node.get("inner"):
            self.note_enum(node, where, line)

        if kind == "CallExpr" and node.get("inner"):
            self.in_printf_source = (call_where or "").endswith(PRINTF_SOURCE)
            for message in self.check_call(node["inner"]):
                self.formats.append((call_where, call_line, message))

        for child in node.get("inner", []):
            if isinstance(child, dict):
                self.walk(child, in_function or kind == "FunctionDecl")

    def note_enum(
        self, node: Dict[str, Any], where: Optional[str], line: int
    ) -> None:
        name = node.get("name")
        fixed = node.get("fixedUnderlyingType")

        if fixed is None:
            what = f"enum {name}" if name else "an anonymous enum"
            self.enums.append((
                where, line,
                f"{what} has no type, declare it as "
                f"enum {name or '<name>'} : <type>"
            ))
            return

        if name:
            sugared = bare_type(fixed.get("qualType", ""))
            plain = bare_type(fixed.get("desugaredQualType", sugared))
            self.enum_types[name] = (sugared, plain)

    def type_name(self, sugared: str) -> str:
        if not sugared.startswith("enum "):
            return sugared

        fixed = self.enum_types.get(sugared[len("enum "):])
        if fixed is None:
            return sugared

        return f"{sugared} : {fixed[0]}"

    def type_rule(self, sugared: str, plain: str) -> Optional[Rule]:
        if sugared.startswith("enum "):
            fixed = self.enum_types.get(sugared[len("enum "):])
            if fixed is None:
                return BUILTIN_RULES.get(plain)
            sugared, plain = fixed

        if sugared == "error_t" and self.in_printf_source:
            return INT_RULE
        if sugared in TYPEDEF_RULES:
            return TYPEDEF_RULES[sugared]
        if CHAR_STRING.match(plain):
            return STRING_RULE

        return BUILTIN_RULES.get(plain)

    def check_call(self, inner: List[Dict[str, Any]]) -> List[str]:
        callee = strip_implicit(inner[0])
        name = callee.get("referencedDecl", {}).get("name")
        if name not in self.format_functions:
            return []

        args = [strip_implicit(arg) for arg in inner[1:]]
        for index, arg in enumerate(args):
            if arg.get("kind") == "StringLiteral":
                return self.check_format(arg["value"][1:-1], args[index + 1:])

        return []

    def check_format(
        self, fmt: str, args: List[Dict[str, Any]]
    ) -> List[str]:
        messages = []
        next_arg = 0

        for match in FORMAT_SPEC.finditer(fmt):
            spec = match.group(0)
            conversion = match.group("conversion")
            if conversion == "%":
                continue

            length = match.group("length") or ""
            next_arg += [match.group("width"),
                         match.group("precision")].count("*")

            if (length not in SUPPORTED_LENGTHS or
                    conversion not in SUPPORTED_CONVERSIONS):
                messages.append(f"{spec} is currently not supported")
                next_arg += 1
                continue

            if next_arg >= len(args):
                break

            arg = args[next_arg]
            next_arg += 1

            if conversion == "p":
                messages.extend(self.check_pointer(fmt, match.end(), arg))
                continue

            message = self.check_value(spec, length, conversion, arg)
            if message is not None:
                messages.append(message)

        return messages

    def check_pointer(
        self, fmt: str, end: int, arg: Dict[str, Any]
    ) -> List[str]:
        extension = next(
            (ext for ext in POINTER_EXTENSIONS if fmt.startswith(ext, end)),
            None
        )

        if extension is None:
            if end < len(fmt) and fmt[end].isupper():
                return [f"%p{fmt[end]}... is not a %p extension, use one "
                        f"of %p{', %p'.join(POINTER_EXTENSIONS)}"]
            return []

        wanted = POINTER_EXTENSION_TYPES.get(extension)
        if wanted is None:
            return []

        sugared = node_types(arg)[0]
        pointee = sugared[:-1].strip() if sugared.endswith("*") else sugared
        if pointee in wanted:
            return []

        return [f"%p{extension} takes a {wanted[0]} *, not {sugared}"]

    def check_value(
        self, spec: str, length: str, conversion: str, arg: Dict[str, Any]
    ) -> Optional[str]:
        sugared, plain = node_types(arg)
        computed = is_computed(arg)

        if computed and sugared == "phys_addr_t":
            return (f"{spec} prints an expression of type phys_addr_t, "
                    f"assign it to a variable and use %pPA, or cast it if "
                    f"it is not an address")
        if computed and mentions_type(arg, "phys_addr_t"):
            return (f"{spec} prints an expression that involves a "
                    f"phys_addr_t, assign it to a phys_addr_t and use %pPA, "
                    f"or cast it if it is not an address")

        rule = self.type_rule(sugared, plain)
        if rule is None:
            return None
        if length in rule.lengths and conversion in rule.conversions:
            return None

        type_name = self.type_name(sugared)
        if computed:
            return (f"{spec} prints an expression of type {type_name}, use "
                    f"{rule.expected} or cast it to the intended type")

        return (f"{spec} gets an argument of type {type_name}, "
                f"use {rule.expected}")


def scan_translation_unit(entry: Dict[str, Any]) -> ScanResult:
    source = entry["file"]
    if not source.endswith(".c") or is_third_party(source):
        return ScanResult([], [], [])
    if any(name in source for name in GENERATED_SOURCES):
        return ScanResult([], [], [])

    command = compiler_command(entry)
    if command is None:
        sys.exit(
            f"{source} was not compiled with clang, the checks that read the"
            " AST need a clang build"
        )

    proc = subprocess.run(
        command, capture_output=True, text=True, cwd=entry["directory"]
    )
    if proc.returncode != 0:
        sys.exit(f"cannot parse {source}:\n{proc.stderr}")

    scanner = AstScanner()
    scanner.walk(json.loads(proc.stdout), False)
    return ScanResult(scanner.variables, scanner.enums, scanner.formats)


@functools.lru_cache(maxsize=None)
def scan_all(ctx: Context) -> List[ScanResult]:
    database = os.path.join(ctx.build_dir, "compile_commands.json")
    with open(database) as f:
        entries = json.load(f)

    with ThreadPoolExecutor(os.cpu_count()) as pool:
        return list(pool.map(scan_translation_unit, entries))


def kernel_path(ctx: Context, where: Optional[str]) -> Optional[str]:
    if where is None:
        return None

    path = os.path.relpath(os.path.realpath(where), ctx.root)
    if not path.startswith("kernel/") or is_third_party(path):
        return None

    return path


def check_variable_prefixes(ctx: Context) -> List[Finding]:
    seen: Set[Finding] = set()

    for unit in scan_all(ctx):
        for where, line, name, prefix in unit.variables:
            path = kernel_path(ctx, where)
            if path is None or name.startswith(prefix):
                continue

            kind = "static" if prefix == "s_" else "global"
            seen.add(Finding(
                path, line,
                f"{kind} variable '{name}' must start with {prefix}"
            ))

    return list(seen)


def located_findings(
    ctx: Context, pick: Callable[[ScanResult], List[Located]]
) -> List[Finding]:
    seen: Set[Finding] = set()

    for unit in scan_all(ctx):
        for where, line, message in pick(unit):
            path = kernel_path(ctx, where)
            if path is not None:
                seen.add(Finding(path, line, message))

    return list(seen)


def check_enum_types(ctx: Context) -> List[Finding]:
    return located_findings(ctx, lambda unit: unit.enums)


def check_formats(ctx: Context) -> List[Finding]:
    return located_findings(ctx, lambda unit: unit.formats)


def check_configs(ctx: Context) -> List[Finding]:
    from scripts.check_configs import check

    return [Finding(path, line, message) for path, line, message in check()]


CHECKS: Dict[str, Check] = {
    "comments": check_comments,
    "configs": check_configs,
    "enum-types": check_enum_types,
    "file-end": check_file_end,
    "formats": check_formats,
    "includes": check_includes,
    "null": check_null,
    "tabs": check_tabs,
    "variable-prefixes": check_variable_prefixes,
}


def run(build_dir: str, checks: Optional[List[str]] = None) -> int:
    ctx = Context(ROOT, os.path.abspath(build_dir))

    findings: List[Finding] = []
    for name in checks or sorted(CHECKS):
        findings.extend(CHECKS[name](ctx))

    for finding in sorted(set(findings)):
        print(f"{finding.path}:{finding.line}: {finding.message}")

    sources = tracked_sources(ctx)
    lines = sum(read_source(ctx, path).count("\n") for path in sources)
    print(f"Checked {len(sources)} files, {lines} lines")

    if findings:
        print(f"{len(set(findings))} problem(s) found")
        return 1

    print("No problems found")
    return 0


def main() -> None:
    parser = argparse.ArgumentParser("Check the kernel coding conventions")
    parser.add_argument("build_dir",
                        help="A configured clang build directory with"
                             " compile_commands.json inside")
    parser.add_argument("--check", action="append", choices=sorted(CHECKS),
                        help="Only run this check, may be repeated")
    args = parser.parse_args()

    sys.path.insert(0, ROOT)
    sys.exit(run(args.build_dir, args.check))


if __name__ == "__main__":
    main()
