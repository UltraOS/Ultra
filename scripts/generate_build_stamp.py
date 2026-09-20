#!/usr/bin/python3
import argparse
import os
import subprocess
import time


# Leaves the file alone when the content is the same, so that nothing
# depending on it is rebuilt
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


def git_revision(source_dir: str) -> str:
    try:
        return subprocess.check_output(
            ["git", "describe", "--always", "--dirty"], cwd=source_dir,
            stderr=subprocess.DEVNULL, text=True
        ).strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "0000000"


def build_date() -> str:
    epoch = os.environ.get("SOURCE_DATE_EPOCH")
    when = time.gmtime(int(epoch)) if epoch else time.gmtime()
    return time.strftime("%a %b %d %H:%M:%S UTC %Y", when)


def c_string(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def make_revision(args: argparse.Namespace) -> None:
    write_if_changed(args.out_file, git_revision(args.source_dir) + "\n")


def make_stamp(args: argparse.Namespace) -> None:
    with open(args.revision_file, "r") as f:
        revision = f.read().strip()

    lines = [
        "#pragma once",
        "",
        "#ifndef IN_BUILD_BANNER",
        "#error Do not include <generated/build_stamp.h> directly, "
        "use <build_banner.h> instead",
        "#endif",
        "",
        "#define ULTRA_BUILD_REVISION " + c_string(revision),
        "#define ULTRA_BUILD_DATE " + c_string(build_date()),
        "",
    ]

    with open(args.out_file, "w") as f:
        f.write("\n".join(lines))


def main() -> None:
    parser = argparse.ArgumentParser("Generate the kernel build stamp")
    commands = parser.add_subparsers(dest="command", required=True)

    revision = commands.add_parser(
        "revision", help="Record the git revision, only if it has changed"
    )
    revision.add_argument("out_file")
    revision.add_argument("--source-dir", required=True)
    revision.set_defaults(handler=make_revision)

    stamp = commands.add_parser(
        "stamp", help="Generate the header holding the build stamp"
    )
    stamp.add_argument("out_file")
    stamp.add_argument("--revision-file", required=True)
    stamp.set_defaults(handler=make_stamp)

    args = parser.parse_args()
    args.handler(args)


if __name__ == "__main__":
    main()
