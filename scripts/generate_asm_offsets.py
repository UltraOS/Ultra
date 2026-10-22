#!/usr/bin/env python3
import argparse
import os
import re


def write_if_changed(path: str, content: str) -> None:
    try:
        with open(path, "r") as f:
            if f.read() == content:
                return
    except FileNotFoundError:
        pass

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(content)


def main() -> None:
    parser = argparse.ArgumentParser("Generate the asm offsets header")
    parser.add_argument("asm_file")
    parser.add_argument("out_file")
    args = parser.parse_args()

    with open(args.asm_file, "r") as f:
        constants = re.findall(r'"->(\w+) (\S+)"', f.read())

    content = "#pragma once\n"
    if constants:
        content += "\n"
        content += "".join(f"#define {k} {v}\n" for k, v in constants)

    write_if_changed(args.out_file, content)


if __name__ == "__main__":
    main()
