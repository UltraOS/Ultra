import os
import sys
from typing import List

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


def make_config(
    layers: List[str], out_path: str, toolchain: str, arch: str
) -> None:
    kconfig = root_kconfig()
    kconfig.warn_assign_undef = True
    kconfig.warn_assign_override = False
    kconfig.warn_assign_redun = False

    arch_sym = kconfig.syms[ARCH_TO_CONFIG_KEY[arch]]
    toolchain_sym = kconfig.syms[TOOLCHAIN_TO_CONFIG_KEY[toolchain]]

    for layer in layers:
        kconfig.load_config(layer, replace=False)

        layer_arch = arch_sym.choice.user_selection
        if layer_arch is not None and layer_arch is not arch_sym:
            sys.exit(f"{layer} selects {layer_arch.name}, which conflicts "
                     f"with --arch {arch}")

        layer_tc = toolchain_sym.choice.user_selection
        if layer_tc is not None and layer_tc is not toolchain_sym:
            print(f"Ignoring {layer_tc.name} from {layer}, "
                  f"building with --toolchain {toolchain}")

        arch_sym.choice.unset_value()
        toolchain_sym.choice.unset_value()

    arch_sym.set_value("y")
    toolchain_sym.set_value("y")
    kconfig.write_config(out_path)
