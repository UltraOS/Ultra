import hashlib
import os
import sys
from typing import Dict, List, Optional, Set

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

MERGE_BASE = ".config.merge-base"
MERGE_BASE_LAYER = "# layer: "


def root_kconfig() -> kc.Kconfig:
    old_dir = os.getcwd()
    os.chdir(ROOT)
    try:
        kconfig = kc.Kconfig(os.path.join(ROOT, "Kconfig"))
    finally:
        os.chdir(old_dir)

    return kconfig


def load_layers(
    layers: List[str], toolchain: str, arch: str
) -> kc.Kconfig:
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

    return kconfig


def display_path(path: str) -> str:
    path = os.path.abspath(path)
    if path.startswith(ROOT + os.sep):
        return os.path.relpath(path, ROOT)

    return path


def config_values(kconfig: kc.Kconfig) -> Dict[str, str]:
    reserved = (
        kconfig.syms[ARCH_TO_CONFIG_KEY["x86_64"]].choice,
        kconfig.syms[TOOLCHAIN_TO_CONFIG_KEY["clang"]].choice,
    )

    return {
        sym.name: sym.str_value for sym in kconfig.unique_defined_syms
        if sym.config_string and sym.choice not in reserved and
        any(node.prompt for node in sym.nodes)
    }


def layer_setting(layers: List[str], name: str) -> str:
    for layer in reversed(layers):
        with open(layer) as f:
            for line in f.read().splitlines():
                if (line.startswith(f"CONFIG_{name}=") or
                   line == f"# CONFIG_{name} is not set"):
                    return display_path(layer)

    return "the config files"


def layer_stamps(layers: List[str]) -> List[str]:
    stamps = []

    for layer in layers:
        with open(layer, "rb") as f:
            digest = hashlib.sha256(f.read()).hexdigest()

        stamps.append(f"{digest} {display_path(layer)}")

    return stamps


def read_merge_base_stamps(path: str) -> Optional[List[str]]:
    if not os.path.isfile(path):
        return None

    with open(path) as f:
        return [
            line[len(MERGE_BASE_LAYER):].rstrip("\n") for line in f
            if line.startswith(MERGE_BASE_LAYER)
        ]


def write_merge_base(kconfig: kc.Kconfig, stamps: List[str],
                     path: str) -> None:
    header = "".join(f"{MERGE_BASE_LAYER}{stamp}\n" for stamp in stamps)
    kconfig.write_config(path, header=header, save_old=False)


def stamp_path(stamp: str) -> str:
    return stamp.split(" ", 1)[1]


def merge_config(
    config: str, merge_base: str, layered: kc.Kconfig, layers: List[str],
    toolchain: str, arch: str
) -> None:
    where = display_path(config)
    stamps = layer_stamps(layers)
    paths = [stamp_path(stamp) for stamp in stamps]
    new_values = config_values(layered)

    reader = root_kconfig()
    reader.warn_to_stderr = False

    old_stamps = read_merge_base_stamps(merge_base)
    if old_stamps is None:
        old_paths = None
        base_values = config_values(load_layers([], toolchain, arch))
    else:
        old_paths = [stamp_path(stamp) for stamp in old_stamps]
        reader.load_config(merge_base)
        base_values = config_values(reader)

    # Leave .config alone when nothing changed, rewriting it would
    # make cmake reconfigure on every build
    if old_stamps == stamps and base_values == new_values:
        return

    reader.load_config(config)
    cur_values = config_values(reader)

    messages = []
    if old_paths is None and paths:
        messages.append(f"now made from {', '.join(paths)}")

    if old_paths is not None and old_paths != paths:
        added = [path for path in paths if path not in old_paths]
        removed = [path for path in old_paths if path not in paths]
        messages.append(f"now made from {', '.join(added) or 'nothing'} "
                        f"instead of {', '.join(removed) or 'nothing'}")

    conflicts = []
    wanted: Dict[str, str] = {}
    kept: Set[str] = set()
    names = sorted(set(base_values) | set(new_values) | set(cur_values))

    for name in names:
        base = base_values.get(name)
        new = new_values.get(name)
        cur = cur_values.get(name)

        if cur == base or (cur is None and new != base):
            if new is not None:
                wanted[name] = new
            continue

        if cur is None:
            continue

        wanted[name] = cur
        kept.add(name)
        if new in (base, cur, None):
            continue

        conflicts.append(f"keeping {name}={cur}, "
                         f"{layer_setting(layers, name)} now gives {new}")

    write_merge_base(layered, stamps, merge_base)

    for name in kept:
        if wanted[name] != new_values.get(name):
            layered.syms[name].set_value(wanted[name])

    hidden = []
    resolved = config_values(layered)
    applied = len([
        name for name in names
        if name not in kept and resolved.get(name) != cur_values.get(name)
    ])

    for name, value in wanted.items():
        if resolved.get(name) == value:
            continue

        depends = kc.expr_str(layered.syms[name].direct_dep)
        if name in kept:
            hidden.append(f"your {name}={value} no longer has an effect, "
                          f"it depends on {depends}")
            continue

        hidden.append(f"{name}={value} from {layer_setting(layers, name)} "
                      f"has no effect, it depends on {depends}")

    if applied:
        changed = [
            stamp_path(stamp) for stamp in stamps
            if stamp not in (old_stamps or [])
        ]
        changes = "change" if applied == 1 else "changes"
        messages.append(f"applied {applied} {changes} from "
                        f"{', '.join(changed) or 'Kconfig'}")

    for message in messages + conflicts + hidden:
        print(f"{where}: {message}")

    layered.write_config(config)


def prepare_config(
    config: str, layers: List[str], toolchain: str, arch: str, reset: bool
) -> None:
    merge_base = os.path.join(os.path.dirname(config), MERGE_BASE)
    layered = load_layers(layers, toolchain, arch)

    if not reset and os.path.isfile(config):
        merge_config(config, merge_base, layered, layers, toolchain, arch)
        return

    write_merge_base(layered, layer_stamps(layers), merge_base)
    layered.write_config(config)
