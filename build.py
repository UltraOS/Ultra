#!/usr/bin/python3
import argparse
import subprocess
import os
import platform
import shutil
import urllib.request
import signal
import sys
from typing import Optional, List

import scripts.build_utils.wsl_wrap as ww
import scripts.build_utils.package_manager as pm
import scripts.build_utils.toolchain_builder as tb
import scripts.build_utils.toolchain_args as ta
import scripts.image_utils.ultra as ultr
import scripts.image_utils.uefi as uefi
import scripts.image_utils.path_guesser as pg

GENERIC_DEPS = {
    "apt": [
        "nasm",
        "xorriso",
        "qemu-system-x86",
        "qemu-system-arm",
        "cmake",
    ],
    "pacman": [
        "nasm",
        "xorriso",
        "qemu-system-x86",
        "qemu-system-arm",
        "cmake",
    ],
    "brew": [
        "nasm",
        "xorriso",
        "qemu",
        "cmake",
    ],
}


def get_toolchain_dir() -> str:
    return pg.project_root_relative("toolchain")


def get_specific_toolchain_dir(type: str, execution_mode: str) -> str:
    return pg.project_root_relative(
        get_toolchain_dir(), f"tools-{type}-{execution_mode}"
    )


def get_build_dir(execution_mode: str, toolchain: str) -> str:
    return pg.project_root_relative(f"build-{toolchain}-{execution_mode}")


def build_toolchain(args: argparse.Namespace, execution_mode: str) -> None:
    if not tb.is_supported_system():
        sys.exit(1)

    tc_root = get_specific_toolchain_dir(args.toolchain, execution_mode)
    tp = ta.params_from_args(args, "elf", tc_root, get_toolchain_dir())

    if not args.skip_generic_dependencies:
        pm.install_dependencies(GENERIC_DEPS)

    tb.build_toolchain(tp)


def build_ultra(
    args: argparse.Namespace, arch: str, execution_mode: str,
    build_dir: str
) -> None:
    rerun_cmake = args.reconfigure or not os.path.isdir(build_dir)

    if rerun_cmake:
        # Only rerun toolchain builder if reconfigure is not artificial
        if not args.reconfigure:
            build_toolchain(args, execution_mode)

        os.makedirs(build_dir, exist_ok=True)
        cmake_args = [f"-DULTRA_ARCH={arch}",
                      f"-DULTRA_ARCH_EXECUTION_MODE={execution_mode}",
                      f"-DULTRA_TOOLCHAIN={args.toolchain}"]
        subprocess.run(["cmake", "..", *cmake_args], check=True, cwd=build_dir)
    else:
        print("Not rerunning cmake since build directory already exists "
              "(--reconfigure)")

    subprocess.run(["cmake", "--build", ".", "-j", str(os.cpu_count())],
                   cwd=build_dir, check=True)


def make_hyper_config(execution_mode: str) -> str:
    return \
f"""
default-entry = ultra-{execution_mode}

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

[ultra-aarch64]
protocol = ultra
higher-half-exclusive = true
binary:
    path = "/kernel-aarch64"
    allocate-anywhere = true

page-table:
    levels = 4
    constraint = exactly

# We don't really need video for now
video-mode = unset
"""  # noqa: E122


def get_kernel_path(execution_node: str, build_dir: str) -> str:
    return os.path.join(build_dir, f"kernel-{execution_node}")


def make_hyper_image(
    br_type: str, fs_type: str, execution_mode: str, build_dir: str,
    hyper_installer: Optional[str], hyper_iso_br: Optional[str],
    hyper_uefi_binaries: List[str], image_path: str
) -> ultr.DiskImage:
    kernel_path = get_kernel_path(execution_mode, build_dir)
    image_root_path = os.path.join(build_dir, "image-root")

    os.makedirs(image_root_path, exist_ok=True)
    shutil.copy(kernel_path, image_root_path)

    return ultr.DiskImage(
        image_root_path, br_type, fs_type,
        hyper_config=make_hyper_config(execution_mode),
        hyper_uefi_binary_paths=hyper_uefi_binaries,
        hyper_iso_br_path=hyper_iso_br,
        hyper_installer_path=hyper_installer,
        out_path=image_path,
    )


def platform_has_native_hyper() -> bool:
    this_os = platform.system()
    this_arch = platform.machine()

    msg = "Hyper loader doesn't get installer releases for {arg}.\n" \
          "Please compile manually and specify with --hyper-installer " \
          "to be able to produce bootable raw images or hybrid ISOs with "\
          "this utility."

    if this_os not in ["Linux", "Windows"]:
        print(msg.format(arg=this_os))
        return False

    if this_arch != "x86_64":
        print(msg.format(arg=this_arch))
        return False

    return True


def hyper_get_binary(name: str, optional: bool = False) -> Optional[str]:
    hyper_version = "v0.10.0"
    root = pg.project_root_relative(f"hyper-{hyper_version}")
    binary_path = os.path.join(root, name)

    if not os.path.isdir(root):
        os.makedirs(root)

    if not os.path.isfile(binary_path):
        base_url = "https://github.com/UltraOS/Hyper/releases/download/"
        base_url += hyper_version
        file_url = f"{base_url}/{name}"
        try:
            urllib.request.urlretrieve(file_url, binary_path)
        except Exception as ex:
            print(f"Failed to retrieve {name}:", ex)
            return None

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
    ret = hyper_get_binary(hyper_get_installer_name())
    assert ret
    return ret


def hyper_get_iso_br() -> str:
    ret = hyper_get_binary("hyper_iso_boot")
    assert ret
    return ret


def run_qemu(
    arch: str, execution_mode: str, image_path: str, image_type: str,
    debug: bool, uefi_boot: bool, uefi_firmware: str
) -> subprocess.Popen:
    extra_args = []
    force_uefi = False

    if arch == "arm":
        extra_args.extend([
            "-M", "virt", "-cpu", "max",
            "-serial", "stdio"
        ])
        qemu_postfix = execution_mode
        force_uefi = True
    else:
        if execution_mode == "x86_64" or uefi_boot:
            qemu_postfix = "x86_64"
        else:
            qemu_postfix = "i386"

        extra_args.extend([
            "-M", "q35", "-debugcon", "stdio"
        ])

    disk_arg = "-cdrom" if image_type == "iso" else "-hda"

    if debug:
        extra_args.extend(["-s", "-S"])

    args = [
        f"qemu-system-{qemu_postfix}",
        disk_arg, image_path, "-m", "1G",
        *extra_args
    ]

    if uefi_boot:
        if uefi_firmware is None:
            uefi_firmware = uefi.get_path_to_qemu_uefi_firmware(qemu_postfix)

        if uefi_firmware is not None:
            drive_opts = f"file={uefi_firmware}"
            drive_opts += ",if=pflash,format=raw,readonly=on"
            args.extend(["-drive", drive_opts])
        elif force_uefi:
            raise RuntimeError(
                f"Unable to boot {arch}/{execution_mode} without UEFI firmware"
            )

    qp = subprocess.Popen(args, start_new_session=debug)
    if not debug:
        try:
            qp.wait()
        except KeyboardInterrupt:
            pass

    return qp


def main() -> None:
    ww.relaunch_in_wsl_if_windows()
    pg.set_project_root(os.path.dirname(os.path.abspath(__file__)))

    parser = argparse.ArgumentParser("Build & run the UltraOS kernel")
    ta.add_base_args(parser)
    parser.add_argument("--arch", default="x86_64",
                        choices=["x86_64", "i686", "aarch64", "aarch32"],
                        help="architecture (execution mode) to "
                             "build the kernel for")
    parser.add_argument("--skip-generic-dependencies", action="store_true",
                        help="don't attempt to fetch the generic dependencies")
    parser.add_argument("--make-image",
                        help="Produce a bootable image after building")
    parser.add_argument("--image-type", choices=["iso", "raw"], default="iso",
                        help="Image type to produce (with --make-image)")
    parser.add_argument("--run", action="store_true",
                        help="Automatically run in QEMU after building")
    parser.add_argument("--uefi", action="store_true",
                        help="Boot in UEFI mode")
    parser.add_argument("--uefi-firmware-path",
                        help="Path to UEFI firmware to use with QEMU")
    parser.add_argument("--debug", action="store_true",
                        help="Start a debugging session after building "
                             "(implies --run)")
    parser.add_argument("--ide-debug", action="store_true",
                        help="Start QEMU in debug mode but don't start "
                             "a debugger")
    parser.add_argument("--hyper-installer", type=str,
                        help="Path to the hyper installer")
    parser.add_argument("--hyper-iso-loader", type=str,
                        help="Path to the hyper iso boot record "
                             "(hyper_iso_boot)")
    parser.add_argument("--hyper-uefi-binary-paths", nargs='+',
                        help="Paths to the hyper UEFI binaries "
                             "(BOOT{X64,AA64}.EFI)")
    parser.add_argument("--no-build", action="store_true",
                        help="Assume the kernel is already built")
    parser.add_argument("--reconfigure", action="store_true",
                        help="Reconfigure cmake before building")
    args = parser.parse_args()

    this_os = platform.system()
    if this_os not in ["Linux", "Darwin", "Windows"]:
        print(f"{this_os} is not (yet) supported")
        sys.exit(1)

    execution_mode = args.arch
    arch = "x86"

    if execution_mode == "i686" or execution_mode == "x86_64":
        arch = "x86"
    elif execution_mode == "aarch32" or execution_mode == "aarch64":
        arch = "arm"
        args.uefi = True

    build_dir = get_build_dir(execution_mode, args.toolchain)

    if not args.no_build:
        build_ultra(args, arch, execution_mode, build_dir)

    is_debug = args.debug or args.ide_debug
    should_run = args.run or is_debug

    if should_run or args.make_image:
        hyper_installer = args.hyper_installer
        hyper_iso_br = args.hyper_iso_loader
        hyper_uefi_binary_paths = args.hyper_uefi_binary_paths

        if args.image_type == "iso":
            fs_type = "ISO9660"
            image_name = "image.iso"
        else:
            fs_type = "FAT32"
            image_name = "image.raw"

        if not hyper_installer:
            if platform_has_native_hyper():
                hyper_installer = hyper_get_installer()
            elif args.image_type != "iso":  # only mandatory for HDDs
                sys.exit(1)

        if not hyper_iso_br:
            hyper_iso_br = hyper_get_iso_br()

        if not hyper_uefi_binary_paths:
            hyper_uefi_binary_paths = [
                hyper_get_binary("BOOTX64.EFI", True),
                hyper_get_binary("BOOTAA64.EFI", True),
            ]
            hyper_uefi_binary_paths = filter(lambda x: x is not None,
                                             hyper_uefi_binary_paths)

        image_path = os.path.join(build_dir, image_name)

        make_hyper_image("MBR", fs_type, args.arch, build_dir, hyper_installer,
                         hyper_iso_br, hyper_uefi_binary_paths, image_path)

    if should_run:
        uefi_boot = hyper_uefi_binary_paths and args.uefi

        qp = run_qemu(arch, execution_mode, image_path, args.image_type,
                      is_debug, uefi_boot, args.uefi_firmware_path)

    if args.debug:
        gdb_args = ["gdb", "--tui", get_kernel_path(args.arch, build_dir),
                    "--eval-command", "target remote localhost:1234"]

        signal.signal(signal.SIGINT, signal.SIG_IGN)
        subprocess.run(gdb_args)
        qp.kill()


if __name__ == "__main__":
    main()
