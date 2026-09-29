import os
import sys

import scripts.kconfiglib.kconfiglib as kc


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ARCH_TO_CONFIG_KEY = {
    "x86_64": "ARCH_X86_64",
    "aarch64": "ARCH_AARCH64",
}

TOOLCHAIN_TO_CONFIG_KEY = {
    "clang": "TOOLCHAIN_CLANG",
    "gcc": "TOOLCHAIN_GCC",
}


def root_kconfig() -> kc.Kconfig:
    old_dir = os.getcwd()
    os.chdir(ROOT)
    try:
        kconfig = kc.Kconfig(os.path.join(ROOT, "Kconfig"))
    finally:
        os.chdir(old_dir)

    return kconfig


def make_config_from_preset(
    preset: str, out_path: str, toolchain: str, arch: str
) -> str:
    kconfig = root_kconfig()
    kconfig.load_config(preset)

    preset_arch = kconfig.syms["ARCH_STRING"].str_value
    if arch != "auto" and arch != preset_arch:
        sys.exit(f"--arch {arch} conflicts with {preset}, "
                 f"which is for {preset_arch}")

    toolchain_sym = kconfig.syms[TOOLCHAIN_TO_CONFIG_KEY[toolchain]]
    preset_toolchain = toolchain_sym.choice.user_selection

    if preset_toolchain is not None and preset_toolchain is not toolchain_sym:
        print(f"Ignoring {preset_toolchain.name} from {preset}, "
              f"building with --toolchain {toolchain}")

    toolchain_sym.set_value("y")
    kconfig.write_config(out_path)

    return preset_arch


def make_default_config(out_path: str, toolchain: str, arch: str) -> None:
    kconfig = root_kconfig()
    kconfig.syms[TOOLCHAIN_TO_CONFIG_KEY[toolchain]].set_value("y")
    kconfig.syms[ARCH_TO_CONFIG_KEY[arch]].set_value("y")
    kconfig.write_config(out_path)
