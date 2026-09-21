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


class VariableScanner:
    def __init__(self) -> None:
        self.current_file: Optional[str] = None
        self.current_line = 0
        self.found: List[Tuple[Optional[str], int, str, str]] = []

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
        self.note_location(extent.get("begin"))
        self.note_location(extent.get("end"))

        kind = node.get("kind")
        if kind == "VarDecl" and "name" in node:
            storage = node.get("storageClass")

            if storage == "static":
                self.found.append((where, line, node["name"], "s_"))
            elif not in_function and storage in (None, "extern"):
                self.found.append((where, line, node["name"], "g_"))

        for child in node.get("inner", []):
            if isinstance(child, dict):
                self.walk(child, in_function or kind == "FunctionDecl")


def scan_translation_unit(
    entry: Dict[str, Any]
) -> List[Tuple[Optional[str], int, str, str]]:
    source = entry["file"]
    if not source.endswith(".c") or is_third_party(source):
        return []
    if any(name in source for name in GENERATED_SOURCES):
        return []

    command = compiler_command(entry)
    if command is None:
        sys.exit(
            f"{source} was not compiled with clang, the variable name check"
            " needs a clang build"
        )

    proc = subprocess.run(
        command, capture_output=True, text=True, cwd=entry["directory"]
    )
    if proc.returncode != 0:
        sys.exit(f"cannot parse {source}:\n{proc.stderr}")

    scanner = VariableScanner()
    scanner.walk(json.loads(proc.stdout), False)
    return scanner.found


def check_variable_prefixes(ctx: Context) -> List[Finding]:
    database = os.path.join(ctx.build_dir, "compile_commands.json")
    with open(database) as f:
        entries = json.load(f)

    with ThreadPoolExecutor(os.cpu_count()) as pool:
        per_unit = list(pool.map(scan_translation_unit, entries))

    seen: Set[Finding] = set()
    for where, line, name, prefix in (v for unit in per_unit for v in unit):
        if where is None or name.startswith(prefix):
            continue

        path = os.path.relpath(os.path.realpath(where), ctx.root)
        if not path.startswith("kernel/") or is_third_party(path):
            continue

        kind = "static" if prefix == "s_" else "global"
        seen.add(Finding(
            path, line, f"{kind} variable '{name}' must start with {prefix}"
        ))

    return list(seen)


CHECKS: Dict[str, Check] = {
    "comments": check_comments,
    "file-end": check_file_end,
    "includes": check_includes,
    "null": check_null,
    "tabs": check_tabs,
    "variable-prefixes": check_variable_prefixes,
}


def run(build_dir: str, checks: Optional[List[str]] = None) -> int:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ctx = Context(root, os.path.abspath(build_dir))

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

    sys.exit(run(args.build_dir, args.check))


if __name__ == "__main__":
    main()
