import os
import re
import tempfile
from typing import List, Tuple

import scripts.config_layers as cl
import scripts.kconfiglib.kconfiglib as kc


Problem = Tuple[str, int, str]
ConfigBuild = Tuple[List[str], str, str]

ASSIGNMENT_RE = re.compile(r"^(?:CONFIG_(\w+)=|# CONFIG_(\w+) is not set$)")
WARNING_RE = re.compile(r"^.*:(\d+): warning: (.*)$")


def resolve(
    kconfig: kc.Kconfig, layers: List[str], arch: str, toolchain: str
) -> str:
    kconfig.unset_values()
    for layer in layers:
        kconfig.load_config(layer, replace=False)

    kconfig.syms[cl.ARCH_TO_CONFIG_KEY[arch]].set_value("y")
    kconfig.syms[cl.TOOLCHAIN_TO_CONFIG_KEY[toolchain]].set_value("y")

    return "".join(sym.config_string for sym in kconfig.unique_defined_syms)


def is_arch_kconfig(path: str) -> bool:
    parts = path.split("/")
    return parts[:2] == ["kernel", "arch"] and len(parts) > 3


def builds_for(arch: str, below: List[str]) -> List[ConfigBuild]:
    return [(below, arch, tc) for tc in cl.TOOLCHAIN_TO_CONFIG_KEY]


def line_matters(
    kconfig: kc.Kconfig, path: str, lines: List[str], i: int,
    builds: List[ConfigBuild], scratch: str
) -> bool:
    without = os.path.join(scratch, "without.config")
    with open(without, "w") as f:
        f.write("".join(f"{x}\n" for x in lines[:i] + lines[i + 1:]))

    return any(
        resolve(kconfig, below + [path], arch, tc) !=
        resolve(kconfig, below + [without], arch, tc)
        for below, arch, tc in builds
    )


def check_file(
    kconfig: kc.Kconfig, path: str, is_preset: bool,
    builds: List[ConfigBuild], scratch: str
) -> List[Problem]:
    problems = []
    where = os.path.relpath(path, cl.ROOT)
    reserved = (
        kconfig.syms[cl.ARCH_TO_CONFIG_KEY["x86_64"]].choice,
        kconfig.syms[cl.TOOLCHAIN_TO_CONFIG_KEY["clang"]].choice,
    )

    kconfig.warnings = []
    kconfig.load_config(path)

    for warning in kconfig.warnings:
        match = WARNING_RE.match(warning)
        if match is None:
            problems.append((where, 1, warning))
            continue

        problems.append((where, int(match.group(1)), match.group(2)))

    with open(path) as f:
        lines = f.read().splitlines()

    for i, line in enumerate(lines):
        match = ASSIGNMENT_RE.match(line)
        if match is None:
            continue

        name = match.group(1) or match.group(2)
        sym = kconfig.syms.get(name)
        if sym is None or not sym.nodes:
            continue

        if sym.choice in reserved:
            problems.append((where, i + 1, f"{name} is chosen by --arch or "
                                           "--toolchain, not by config "
                                           "files"))
            continue

        if is_preset and all(is_arch_kconfig(n.filename) for n in sym.nodes):
            problems.append((where, i + 1, f"{name} only exists for one "
                                           "arch, set it in configs/arch/"
                                           "<arch>/ instead"))
            continue

        if line_matters(kconfig, path, lines, i, builds, scratch):
            continue

        problems.append((where, i + 1, f"'{line}' changes nothing for any "
                                       "arch and toolchain"))

    return problems


def stray_arch_files() -> List[Problem]:
    problems = []
    arch_root = os.path.join(cl.ROOT, "configs", "arch")

    for arch in sorted(os.listdir(arch_root)):
        for name in sorted(os.listdir(os.path.join(arch_root, arch))):
            where = f"configs/arch/{arch}/{name}"

            if arch not in cl.ARCH_TO_CONFIG_KEY:
                problems.append((where, 1, f"{arch} is not a known arch"))
                continue

            if name != "base.config":
                problems.append((where, 1, "only base.config is currently "
                                           "supported in an arch directory"))

    return problems


def check_all(kconfig: kc.Kconfig, scratch: str) -> List[Problem]:
    problems = stray_arch_files()
    preset_builds: List[ConfigBuild] = []

    for arch in cl.ARCH_TO_CONFIG_KEY:
        base = cl.arch_base_config(arch)
        if base is None:
            preset_builds.extend(builds_for(arch, []))
            continue

        preset_builds.extend(builds_for(arch, [base]))
        problems.extend(check_file(kconfig, base, False,
                                   builds_for(arch, []), scratch))

    configs_dir = os.path.join(cl.ROOT, "configs")
    for name in sorted(os.listdir(configs_dir)):
        if not name.endswith(".config"):
            continue

        problems.extend(check_file(kconfig, os.path.join(configs_dir, name),
                                   True, preset_builds, scratch))

    return problems


def check() -> List[Problem]:
    kconfig = cl.root_kconfig()
    kconfig.warn_to_stderr = False
    kconfig.warn_assign_undef = True
    kconfig.warn_assign_override = False
    kconfig.warn_assign_redun = False

    with tempfile.TemporaryDirectory() as scratch:
        return check_all(kconfig, scratch)
