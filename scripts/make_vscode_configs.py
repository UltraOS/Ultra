#!/usr/bin/python3
import os
import json

# Automatically generate .vscode launch.json & tasks.json for kernel debugging
# and building. This includes all architectures and toolchains.
# ----------------------------------------------------------------------------
# The reason we need this is because of how icredibly limited VSCode's project
# files are. There isn't a simple way to define variables/parameteres/matricies
# or any non-hacky way of passing arguments to tasks. Therefore we would have
# to hand write every task N*M times where N is the number of supported
# toolchains and M is the number of supported architectures, same goes for
# launch.json. Instead, generate them automatically.

VSCODE_FILE_VERSION = "2.0.0"

COMPILERS = {
    "clang",
    "gcc"
}

ARCHITECTURES = {
    "i686",
    "x86_64"
}


def make_rebuild_task_label(compiler, arch):
    return f"rebuild-kernel-{compiler}-{arch}"


def make_rebuild_task(compiler, arch):
    task_template = {
        "label": make_rebuild_task_label(compiler, arch),
        "command": "python3",
        "args": ["${workspaceRoot}/build.py", "--arch", arch],
        "group": {
            "kind": "build",
            "isDefault": True
        }
    }

    return task_template


def make_debug_task_label(compiler, arch):
    return f"rebuild-kernel-and-start-qemu-{compiler}-{arch}"


def make_debug_task(compiler, arch):
    task_template = {
        "label": make_debug_task_label(compiler, arch),
        "command": "python3",
        "args": ["${workspaceRoot}/build.py", "--arch", arch, "--ide-debug", "--no-build"],
        "dependsOn": make_rebuild_task_label(compiler, arch)
    }

    return task_template


def make_launch_command(compiler, arch):
    launch_template = {
        "name": f"Build & debug {arch} with {compiler}",
        "type": "cppdbg",
        "request": "launch",
        "MIMode": "gdb",
        "setupCommands": [
            {
                "text": "target remote localhost:1234",
            }
        ],
        "cwd": "${workspaceRoot}",
        "program": f"${{workspaceRoot}}/build-{compiler}-{arch}/kernel-{arch}",
        "preLaunchTask": make_debug_task_label(compiler, arch),
    }

    return launch_template


def project_root():
    this_file = os.path.abspath(__file__)
    pardir = os.path.join(this_file, os.pardir, os.pardir)

    return os.path.abspath(pardir)


def make_vscode_template(arr_key, out_file, gen_fns):
    objects = {
        "version": VSCODE_FILE_VERSION,
        arr_key: []
    }

    for compiler in COMPILERS:
        for arch in ARCHITECTURES:
            for gen_fn in gen_fns:
                objects[arr_key].append(gen_fn(compiler, arch))

    with open(out_file, "w") as f:
        json.dump(objects, f, indent=4)


def main():
    root = project_root()
    vscode_dir = os.path.join(root, ".vscode")

    if os.path.exists(vscode_dir):
        if not os.path.isdir(vscode_dir):
            raise RuntimeError(".vscode already exists and is not a directory")
    else:
        os.mkdir(vscode_dir)

    tasks_json = os.path.join(vscode_dir, "tasks.json")
    make_vscode_template("tasks", tasks_json,
                         [make_rebuild_task, make_debug_task])

    launch_json = os.path.join(vscode_dir, "launch.json")
    make_vscode_template("configurations", launch_json, [make_launch_command])


if __name__ == "__main__":
    main()
