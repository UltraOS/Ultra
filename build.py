#!/usr/bin/python3
import argparse
import subprocess
import os
import platform
import shutil
import urllib.request
import signal
import sys
from contextlib import contextmanager
from typing import Optional, List, Callable, Iterator

try:
    import scripts.kconfiglib.kconfiglib as kc
    # Make sure the rest of "import kconfiglib" always references this and not
    # the kconfiglib from pip or other copies of the library
    sys.modules["kconfiglib"] = kc

    import scripts.build_utils.wsl_wrap as ww
    import scripts.build_utils.package_manager as pm
    import scripts.build_utils.toolchain_builder as tb
    import scripts.build_utils.toolchain_args as ta
    import scripts.image_utils.ultra as ultr
    import scripts.image_utils.uefi as uefi
    import scripts.image_utils.path_guesser as pg
except ImportError:
    print("Unable to import one of submodule libraries!")
    print("Please run 'git submodule update --init' to initialize submodules")
    sys.exit(1)

GENERIC_DEPS = {
    "apt": [
        "nasm",
        "xorriso",
        "qemu-system-x86",
        "qemu-system-arm",
        "cmake",
        "mtools",
        "python3",
        "python3-pyelftools",
        "python3-tk",
    ],
    "pacman": [
        "nasm",
        "xorriso",
        "qemu-system-x86",
        "qemu-system-arm",
        "cmake",
        "mtools",
        "python",
        "python-pyelftools",
        "tk",
    ],
    "brew": [
        "nasm",
        "xorriso",
        "qemu",
        "cmake",
        "mtools",
        "python3",
        "python-tk",
    ],
}

ARCH_TO_CONFIG_KEY = {
    "x86_64": "ARCH_X86_64",
    "aarch64": "ARCH_AARCH64",
}

TOOLCHAIN_TO_CONFIG_KEY = {
    "clang": "TOOLCHAIN_CLANG",
    "gcc": "TOOLCHAIN_GCC",
}


@contextmanager
def enter_work_dir(path: str) -> Iterator[None]:
    old_dir = os.getcwd()
    os.chdir(path)
    try:
        yield
    finally:
        os.chdir(old_dir)


def get_toolchain_dir() -> str:
    return pg.project_root_relative("toolchain")


def get_specific_toolchain_dir(type: str, arch: str) -> str:
    return pg.project_root_relative(
        get_toolchain_dir(), f"tools-{type}-{arch}"
    )


def get_build_dir(suffix: str, toolchain: str) -> str:
    return pg.project_root_relative(f"build-{toolchain}-{suffix}")


def get_tests_dir() -> str:
    return pg.project_root_relative("tests")


def get_tests_build_dir(this_os: str) -> str:
    return pg.project_root_relative(get_tests_dir(), f"build-{this_os}")


def platform_name_for_binary(binary: str, this_os: str) -> str:
    if this_os == "windows":
        binary += ".exe"

    return binary


def test_runner_binary(this_os: str) -> str:
    return os.path.join(
        "tests", "bin", platform_name_for_binary("run_tests", this_os)
    )


def build_toolchain(args: argparse.Namespace) -> None:
    if not tb.is_supported_system():
        sys.exit(1)

    tc_root = get_specific_toolchain_dir(args.toolchain, args.arch)
    tp = ta.params_from_args(args, "elf", tc_root, get_toolchain_dir())

    if not args.skip_generic_dependencies:
        pm.install_dependencies(GENERIC_DEPS)

    tb.build_toolchain(tp)


def cmake_build(
    args: argparse.Namespace, build_dir: str, extra_args: List[str] = [],
    reconfigure_cb: Optional[Callable[[], None]] = None
) -> None:
    cmake_cache = os.path.join(build_dir, "CMakeCache.txt")
    rerun_cmake = args.reconfigure or not os.path.isfile(cmake_cache)

    if rerun_cmake:
        if reconfigure_cb is not None:
            reconfigure_cb()
        os.makedirs(build_dir, exist_ok=True)
        subprocess.run(["cmake", "..", *extra_args], check=True, cwd=build_dir)
    else:
        print("Not rerunning cmake since build directory already exists "
              "(--reconfigure)")

    subprocess.run(["cmake", "--build", ".", "-j", str(os.cpu_count())],
                   cwd=build_dir, check=True)


def build_ultra(
    args: argparse.Namespace, build_dir: str
) -> None:
    def rebuild_toolchain() -> None:
        # Only rerun toolchain builder if reconfigure is not artificial
        if not args.reconfigure:
            build_toolchain(args)

    cmake_build(
        args, build_dir, [f"-DCONFIG_FILE={args.config}"],
        rebuild_toolchain
    )


def make_hyper_config(arch: str) -> str:
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

cmdline = "earlycon=e9"

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
    br_type: str, fs_type: str, arch: str, build_dir: str,
    hyper_installer: Optional[str], hyper_iso_br: Optional[str],
    hyper_uefi_binaries: List[str], image_path: str, force_regenerate: bool
) -> Optional[ultr.DiskImage]:
    kernel_path = get_kernel_path(arch, build_dir)
    image_root_path = os.path.join(build_dir, "image-root")

    try:
        if (not force_regenerate and
           os.path.getmtime(kernel_path) < os.path.getmtime(image_path)):
            print("Image is newer than the kernel binary, not regenerating "
                  "(--make-image)")
            return None
    except OSError:
        pass

    os.makedirs(image_root_path, exist_ok=True)
    shutil.copy(kernel_path, image_root_path)

    return ultr.DiskImage(
        image_root_path, br_type, fs_type,
        hyper_config=make_hyper_config(arch),
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
    arch: str, image_path: str, image_type: str, debug: bool, uefi_boot: bool,
    uefi_firmware: str, kvm: bool, dry: bool
) -> Optional[subprocess.Popen]:
    extra_args = []
    force_uefi = False

    if arch == "aarch64":
        extra_args.extend([
            "-M", "virt", "-cpu", "max",
            "-serial", "stdio"
        ])
        force_uefi = True
    elif arch == "x86_64":
        extra_args.extend([
            "-M", "q35", "-debugcon", "stdio"
        ])
    else:
        raise RuntimeError(f"Unknown/unsupported architecture '{arch}'")

    disk_arg = "-cdrom" if image_type == "iso" else "-hda"

    if debug:
        extra_args.extend(["-s", "-S"])

    if kvm:
        extra_args.append("--enable-kvm")

    args = [
        f"qemu-system-{arch}",
        disk_arg, image_path, "-m", "1G",
        *extra_args
    ]

    if uefi_boot:
        if uefi_firmware is None:
            uefi_firmware = uefi.get_path_to_qemu_uefi_firmware(arch)

        if uefi_firmware is not None:
            drive_opts = f"file={uefi_firmware}"
            drive_opts += ",if=pflash,format=raw,readonly=on"
            args.extend(["-drive", drive_opts])
        elif force_uefi:
            raise RuntimeError(
                f"Unable to boot {arch} without UEFI firmware"
            )

    if dry:
        print(" ".join(args))
        return None

    qp = subprocess.Popen(args, start_new_session=debug)
    if not debug:
        try:
            qp.wait()
        except KeyboardInterrupt:
            pass

    return qp


def run_unit_tests(args: argparse.Namespace, this_os: str) -> int:
    dir = get_tests_build_dir(this_os)

    cmake_build(args, dir)
    binary = pg.project_root_relative(test_runner_binary(this_os))
    return subprocess.run([binary]).returncode


def root_kconfig() -> kc.Kconfig:
    with enter_work_dir(pg.project_root()):
        kconfig = kc.Kconfig(pg.project_root_relative("Kconfig"))

    return kconfig


def config_sanitize(path: str) -> None:
    kconfig = root_kconfig()
    kconfig.load_config(path)
    kconfig.write_config(path)


def config_get_arch(path: str) -> str:
    kconfig = root_kconfig()
    kconfig.load_config(path)
    return kconfig.syms["ARCH_STRING"].str_value


def make_default_config(out_path: str, toolchain: str, arch: str) -> None:
    kconfig = root_kconfig()
    kconfig.syms[TOOLCHAIN_TO_CONFIG_KEY[toolchain]].set_value("y")
    kconfig.syms[ARCH_TO_CONFIG_KEY[arch]].set_value("y")
    kconfig.write_config(out_path)


def main() -> None:
    ww.relaunch_in_wsl_if_windows()
    pg.set_project_root(os.path.dirname(os.path.abspath(__file__)))

    parser = argparse.ArgumentParser("Build & run the UltraOS kernel")
    ta.add_base_args(parser)
    parser.add_argument("--arch", default="auto",
                        choices=["auto", "x86_64", "aarch64"],
                        help="CPU architecture to build the kernel for "
                             "(auto implies x86_64 or the config setting if "
                             "--config is specified)")
    parser.add_argument("--skip-generic-dependencies", action="store_true",
                        help="don't attempt to fetch the generic dependencies")
    parser.add_argument("--make-image",
                        help="Produce a bootable image after building")
    parser.add_argument("--image-type", choices=["iso", "raw"], default="iso",
                        help="Image type to produce (with --make-image)")
    parser.add_argument("--run", action="store_true",
                        help="Automatically run in QEMU after building")
    parser.add_argument("--kvm", action="store_true",
                        help="Run QEMU with KVM enabled (implies --run)")
    parser.add_argument("--dry", action="store_true",
                        help="Dump the QEMU command line instead of running")
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
    parser.add_argument("--unit-tests", action="store_true",
                        help="Run the userspace test suite")
    parser.add_argument("--config",
                        help="Configuration file to use for this build")
    parser.add_argument("--menuconfig", action="store_true",
                        help="Run menuconfig to edit the current config file")
    parser.add_argument("--guiconfig", action="store_true",
                        help="Run guiconfig to edit the current config file")
    args = parser.parse_args()

    this_os = platform.system()
    if this_os not in ["Linux", "Darwin", "Windows"]:
        print(f"{this_os} is not (yet) supported")
        sys.exit(1)

    if args.unit_tests:
        sys.exit(run_unit_tests(args, this_os.lower()))

    if args.config and args.arch != "auto":
        sys.exit(
            "Provided both --arch and a custom --config!\n"
            "Config already provides an arch, please choose one"
        )

    if args.arch == "auto" and not args.config:
        args.arch = "x86_64"

    if args.config:
        if not os.path.isfile(args.config):
            raise RuntimeError(f"Invalid --config path: {args.config}")

        config_sanitize(args.config)
        args.arch = config_get_arch(args.config)

        build_dir = pg.project_root_relative("build-user-config")
        os.makedirs(build_dir, exist_ok=True)
    else:
        build_dir = pg.project_root_relative(
            f"build-{args.toolchain}-{args.arch}"
        )
        os.makedirs(build_dir, exist_ok=True)

        args.config = os.path.join(build_dir, ".config")
        if not os.path.isfile(args.config):
            make_default_config(args.config, args.toolchain, args.arch)

    if args.menuconfig or args.guiconfig:
        os.environ["KCONFIG_CONFIG"] = args.config

        import scripts.kconfiglib.menuconfig as mc
        import scripts.kconfiglib.guiconfig as gc

        module = mc if args.menuconfig else gc

        with enter_work_dir(pg.project_root()):
            module.menuconfig(root_kconfig())
        sys.exit(0)

    if not args.no_build:
        build_ultra(args, build_dir)

    is_debug = args.debug or args.ide_debug
    should_run = args.run or args.kvm or is_debug

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

        make_hyper_image(
            "MBR", fs_type, args.arch, build_dir,
            hyper_installer, hyper_iso_br, hyper_uefi_binary_paths,
            image_path, args.make_image
        )

    if should_run:
        if args.arch == "aarch64":
            args.uefi = True
        uefi_boot = hyper_uefi_binary_paths and args.uefi

        qp = run_qemu(args.arch, image_path, args.image_type, is_debug,
                      uefi_boot, args.uefi_firmware_path, args.kvm, args.dry)

    if args.debug:
        assert qp
        gdb_args = ["gdb", "--tui", get_kernel_path(args.arch, build_dir),
                    "--eval-command", "target remote localhost:1234"]

        signal.signal(signal.SIGINT, signal.SIG_IGN)
        subprocess.run(gdb_args)
        qp.kill()


if __name__ == "__main__":
    main()
