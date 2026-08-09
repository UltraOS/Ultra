#!/usr/bin/python3
import argparse
import os
import sys
from typing import Dict

# Make sure it's possible to run the script both directly and as a module
PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if PROJECT_ROOT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT)

try:
    import scripts.kconfiglib.kconfiglib as kconfiglib
except ImportError:
    print("Unable to import kconfiglib!")
    print("Please run 'git submodule update --init' to initialize submodules")
    sys.exit(1)

# Maps symbol names to their last seen values
STATE_NAME = "values"

# The set of all currently known symbol names
MANIFEST_NAME = "manifest"


def read_state(path: str) -> Dict[str, str]:
    state = {}

    try:
        with open(path, "r") as f:
            for line in f:
                name, _, value = line.rstrip("\n").partition("=")
                state[name] = value
    except FileNotFoundError:
        pass

    return state


def touch(path: str) -> None:
    with open(path, "a"):
        os.utime(path, None)


def write_if_different(path: str, data: str) -> None:
    try:
        with open(path, "r") as f:
            if f.read() == data:
                return
    except FileNotFoundError:
        pass

    with open(path, "w") as f:
        f.write(data)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Maintain a directory of per-symbol stamp files "
                    "(an equivalent of the kernel's include/config/), "
                    "touching only the stamps of symbols whose values "
                    "have changed since the last run"
    )
    parser.add_argument("out_dir")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    kconf = kconfiglib.Kconfig()
    kconf.load_config()

    state_path = os.path.join(args.out_dir, STATE_NAME)
    old_values = read_state(state_path)
    new_values = {}

    for sym in kconf.unique_defined_syms:
        # Single-line representation of the value as it would appear
        # in the .config file, empty for symbols that are omitted
        value = sym.config_string.strip()
        new_values[sym.name] = value

        stamp = os.path.join(args.out_dir, sym.name)
        if not os.path.exists(stamp) or \
           old_values.get(sym.name, "") != value:
            touch(stamp)

    # Touch (but never delete) stamps of symbols that no longer exist,
    # so that their dependents get rebuilt and drop the dependency
    for name in old_values:
        if name in new_values:
            continue

        stamp = os.path.join(args.out_dir, name)
        if os.path.exists(stamp):
            touch(stamp)

    state = "".join(
        f"{name}={value}\n" for name, value in sorted(new_values.items())
    )
    write_if_different(state_path, state)

    manifest = "".join(f"{name}\n" for name in sorted(new_values))
    write_if_different(os.path.join(args.out_dir, MANIFEST_NAME), manifest)


if __name__ == "__main__":
    main()
