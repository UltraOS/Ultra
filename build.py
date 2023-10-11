#!/usr/bin/python3
import argparse
import subprocess
import os
import platform
import shutil
import urllib.request
import signal
import sys

import scripts.build_utils.wsl_wrap as ww
import scripts.build_utils.package_manager as pm
import scripts.build_utils.toolchain_builder as tb
import scripts.build_utils.toolchain_args as ta
import scripts.image_utils.ultra as ultr

GENERIC_DEPS = {
    "apt": [
        "nasm",
        "xorriso",
        "qemu-system-x86",
        "cmake",
    ],
    "pacman": [
        "nasm",
        "xorriso",
        "qemu-system-x86",
        "cmake",
    ],
    "brew": [
        "nasm",
        "xorriso",
        "qemu",
        "cmake",
    ],
}


def get_project_root():
    return os.path.dirname(os.path.abspath(__file__))


def get_toolchain_dir():
    return os.path.join(get_project_root(), "toolchain")


def get_specific_toolchain_dir(type, arch):
    return os.path.join(get_toolchain_dir(), f"tools-{type}-{arch}")


def get_build_dir(arch, toolchain):
    return os.path.join(get_project_root(), f"build-{toolchain}-{arch}")


def build_toolchain(args):
    if not tb.is_supported_system():
        exit(1)

    tc_root = get_specific_toolchain_dir(args.toolchain, args.arch)
    tp = ta.params_from_args(args, "elf", tc_root, get_toolchain_dir())

    if not args.skip_generic_dependencies:
        pm.install_dependencies(GENERIC_DEPS)

    tb.build_toolchain(tp)


def build_ultra(args, build_dir):
    rerun_cmake = args.reconfigure or not os.path.isdir(build_dir)

    if rerun_cmake:
        # Only rerun toolchain builder if reconfigure is not artificial
        if not args.reconfigure:
            build_toolchain(args)

        os.makedirs(build_dir, exist_ok=True)
        cmake_args = [f"-DULTRA_ARCH={args.arch}", f"-DULTRA_TOOLCHAIN={args.toolchain}"]
        subprocess.run(["cmake", "..", *cmake_args], check=True, cwd=build_dir)
    else:
        print("Not rerunning cmake since build directory already exists "
              "(--reconfigure)")

    subprocess.run(["cmake", "--build", ".", "-j", str(os.cpu_count())],
                   cwd=build_dir, check=True)


def make_hyper_config(arch):
    return \
f"""
default-entry = ultra-{arch}

[ultra-x86_64]
protocol = ultra
higher-half-exclusive = true

binary:
    path = "/kernel-x86_64"
    allocate-anywhere = true

page-table:
    levels = 5
    constraint = maximum

# We don't really need video for now
video-mode = unset

[ultra-i686]
protocol = ultra
higher-half-exclusive = true
binary = "/kernel-i686"

page-table:
    levels = 3
    constraint = exactly

# We don't really need video for now
video-mode = unset
"""


def get_kernel_path(arch, build_dir):
    return os.path.join(build_dir, f"kernel-{arch}")


def make_hyper_iso(arch, build_dir, hyper_installer,
                   hyper_iso_br, image_path):
    kernel_path = get_kernel_path(arch, build_dir)
    iso_root_path = os.path.join(build_dir, "iso_root")
    cfg_path = os.path.join(iso_root_path, "hyper.cfg")

    os.makedirs(iso_root_path, exist_ok=True)
    shutil.copy(kernel_path, iso_root_path)

    with open(cfg_path, "w") as f:
        cfg = make_hyper_config(arch)
        f.write(cfg)

    return ultr.DiskImage(
        iso_root_path, "MBR", "ISO9660",
        hyper_iso_br_path=hyper_iso_br,
        hyper_installer_path=hyper_installer,
        out_path=image_path,
    )


def platform_has_native_hyper() -> bool:
    this_os = platform.system()
    this_arch = platform.machine()

    msg = "Hyper loader doesn't get installer releases for {arg}.\n" \
          "Please compile manually and specify with --hyper-installer " \
          "to be able to produce ISOs with this utility."

    if this_os not in ["Linux", "Windows"]:
        print(msg.format(arg=this_os))
        return False

    if this_arch != "x86_64":
        print(msg.format(arg=this_arch))
        return False

    return True


def hyper_get_binary(name) -> str:
    hyper_version = "v0.6.0"
    root = os.path.join(get_project_root(), f"hyper-{hyper_version}")
    binary_path = os.path.join(root, name)

    if not os.path.isdir(root):
        os.makedirs(root)

    if not os.path.isfile(binary_path):
        base_url = f"https://github.com/UltraOS/Hyper/releases/download/{hyper_version}"
        file_url = f"{base_url}/{name}"
        urllib.request.urlretrieve(file_url, binary_path)
        os.chmod(binary_path, 0o777)
    return binary_path


def hyper_get_installer_name() -> str:
    # This assumes we're running on a supported system
    this_os = platform.system()
    basename = "hyper_install"

    if this_os == "Windows":
        return f"{basename}-win64.exe"

    return f"{basename}-linux-x86_64"


def hyper_get_installer() -> str:
    return hyper_get_binary(hyper_get_installer_name())


def hyper_get_iso_br() -> str:
    return hyper_get_binary("hyper_iso_boot")


def run_qemu(arch, image_path, debug):
    basename = "qemu-system"

    if arch == "i686":
        qemu_binary = f"{basename}-i386"
    elif arch == "x86_64":
        qemu_binary = f"{basename}-x86_64"

    args = [qemu_binary, "-M", "q35",
            "-cdrom", image_path,
            "-m", "1G",
            "-debugcon", "stdio"]

    if debug:
        args.extend(["-s", "-S"])

    qp = subprocess.Popen(args, start_new_session=debug)
    if not debug:
        try:
            qp.wait()
        except KeyboardInterrupt:
            pass

    return qp


def main():
    ww.relaunch_in_wsl_if_windows()

    parser = argparse.ArgumentParser("Build & run the UltraOS kernel")
    ta.add_base_args(parser)
    parser.add_argument("--arch", choices=["x86_64", "i686"], default="x86_64",
                        help="architecture to build the kernel for")
    parser.add_argument("--skip-generic-dependencies", action="store_true",
                        help="don't attempt to fetch the generic dependencies")
    parser.add_argument("--make-iso", action="store_true",
                        help="Produce a bootable .iso image after building")
    parser.add_argument("--run", action="store_true",
                        help="Automatically run in QEMU after building")
    parser.add_argument("--debug", action="store_true",
                        help="Start a debugging session after building (implies --run)")
    parser.add_argument("--ide-debug", action="store_true",
                        help="Start QEMU in debug mode but don't start a debugger")
    parser.add_argument("--hyper-installer", type=str,
                        help="Path to the hyper installer")
    parser.add_argument("--hyper-iso-loader", type=str,
                        help="Path to the hyper iso boot record (hyper_iso_boot)")
    parser.add_argument("--no-build", action="store_true",
                        help="Assume the kernel is already built")
    parser.add_argument("--reconfigure", action="store_true",
                        help="Reconfigure cmake before building")
    args = parser.parse_args()

    this_os = platform.system()
    if this_os not in ["Linux", "Darwin", "Windows"]:
        print(f"{this_os} is not (yet) supported")
        exit(1)

    build_dir = get_build_dir(args.arch, args.toolchain)

    if not args.no_build:
        build_ultra(args, build_dir)

    is_debug = args.debug or args.ide_debug
    should_run = args.run or is_debug

    if should_run or args.make_iso:
        hyper_installer = args.hyper_installer
        hyper_iso_br = args.hyper_iso_loader

        if not hyper_installer:
            if not platform_has_native_hyper():
                exit(1)

            hyper_installer = hyper_get_installer()

        if not hyper_iso_br:
            hyper_iso_br = hyper_get_iso_br()

        image_path = os.path.join(build_dir, "image.iso")
        make_hyper_iso(args.arch, build_dir, hyper_installer,
                       hyper_iso_br, image_path)

    if should_run:
        qp = run_qemu(args.arch, image_path, is_debug)

    if args.debug:
        gdb_args = ["gdb", "--tui", get_kernel_path(args.arch, build_dir),
                    "--eval-command", "target remote localhost:1234"]

        signal.signal(signal.SIGINT, signal.SIG_IGN)
        subprocess.run(gdb_args)
        qp.kill()


if __name__ == "__main__":
    main()
