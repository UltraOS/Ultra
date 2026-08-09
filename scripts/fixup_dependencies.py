#!/usr/bin/python3
import argparse
import os
import re
import subprocess
import sys
from typing import List, Optional, Set, Tuple

"""
Compiler launcher that rewrites the generated depfile to depend on
per-symbol stamp files (see sync_config_dependencies.py) instead of the
monolithic generated config.h, so that a configuration change only
rebuilds the translation units that use the changed symbols.

A translation unit's set of used symbols is discovered textually.
For this to be sound, configuration checks must only ever be spelled
as either a literal CONFIG_<name> token or an invocation of one of the
config.h helpers matched below: constructing CONFIG_ tokens via ## pasting
outside of config_helpers.h is not allowed.
"""

CONFIG_TOKEN_PATTERN = re.compile(rb"\bCONFIG_([A-Z0-9_]+)\b")
HELPER_PATTERN = re.compile(
    rb"\b(?:IS_ENABLED|IS_DISABLED|IS_BUILTIN|IS_MODULE|IS_REACHABLE)"
    rb"\s*\(\s*([A-Z0-9_]+)\s*\)"
)

MANIFEST_NAME = "manifest"


def find_depfile(command: List[str]) -> Optional[str]:
    depfile = None

    for i, arg in enumerate(command):
        if arg == "-MF" and i + 1 < len(command):
            depfile = command[i + 1]
        elif arg.startswith("-MF") and len(arg) > 3:
            depfile = arg[3:]

    return depfile


# NOTE: no support for escaped spaces in paths
def parse_depfile(text: str) -> Tuple[str, List[str]]:
    text = text.replace("\\\n", " ")

    target, sep, deps = text.partition(":")
    if not sep:
        raise ValueError("no target in depfile")

    return target.strip(), deps.split()


def referenced_symbols(path: str) -> Set[str]:
    with open(path, "rb") as f:
        data = f.read()

    symbols = set(CONFIG_TOKEN_PATTERN.findall(data))
    symbols.update(HELPER_PATTERN.findall(data))
    return {sym.decode() for sym in symbols}


def rewrite_depfile(args: argparse.Namespace, depfile: str) -> None:
    config_h = os.path.abspath(args.config_h)
    skip_scan = {os.path.abspath(path) for path in args.skip_scan}
    skip_scan.add(config_h)

    with open(depfile, "r") as f:
        target, deps = parse_depfile(f.read())

    deps = [os.path.abspath(dep) for dep in deps]
    if config_h not in deps:
        return

    deps.remove(config_h)

    symbols = set()
    for dep in deps:
        if dep in skip_scan or not dep.startswith(args.srctree + os.sep):
            continue

        symbols.update(referenced_symbols(dep))

    with open(os.path.join(args.stamps, MANIFEST_NAME), "r") as f:
        manifest = set(f.read().split())

    stamps = set()
    have_unknown = False

    for sym in symbols:
        if sym in manifest:
            stamps.add(sym)
        elif sym.endswith("_MODULE") and sym[:-len("_MODULE")] in manifest:
            stamps.add(sym[:-len("_MODULE")])
        else:
            # Reference to a symbol that doesn't exist (yet): make the
            # unit depend on the set of known symbols itself, so that
            # it gets rebuilt if the symbol ever comes into existence
            have_unknown = True

    deps.extend(os.path.join(args.stamps, stamp) for stamp in sorted(stamps))
    if have_unknown:
        deps.append(os.path.join(args.stamps, MANIFEST_NAME))

    with open(depfile, "w") as f:
        f.write(f"{target}: \\\n")
        f.write(" \\\n".join(f"  {dep}" for dep in deps))
        f.write("\n")


def main() -> int:
    argv = sys.argv[1:]

    try:
        split = argv.index("--")
    except ValueError:
        print(
            "fixup_dependencies: no compiler command after '--'",
            file=sys.stderr
        )
        return 1

    parser = argparse.ArgumentParser()
    parser.add_argument("--config-h", required=True)
    parser.add_argument("--stamps", required=True)
    parser.add_argument("--srctree", required=True)
    parser.add_argument("--skip-scan", action="append", default=[])
    args = parser.parse_args(argv[:split])
    args.srctree = os.path.abspath(args.srctree)

    command = argv[split + 1:]
    status = subprocess.call(command)
    if status != 0:
        return status

    depfile = find_depfile(command)
    if depfile is not None and os.path.exists(depfile):
        rewrite_depfile(args, depfile)

    return 0


if __name__ == "__main__":
    sys.exit(main())
