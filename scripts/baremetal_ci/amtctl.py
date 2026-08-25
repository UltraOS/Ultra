#!/usr/bin/env python3
# Manual AMT poking for when a machine misbehaves: power control and
# one-shot PXE boots against a single box, bypassing the CI queue, and
# the identity lookups a machine entry needs (its MAC).
# Takes either raw connection details or a server config + machine
# name (which also applies the machine's boot-method quirks).

import argparse
import json
import os
import sys
from typing import Optional

try:
    from . import amt
except ImportError:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import amt  # type: ignore[import-not-found,no-redef]

POWER_LABELS = {
    2: "ON",
    6: "OFF (hard)",
    8: "OFF (soft)",
}


def machine_from_args(args: argparse.Namespace) -> amt.Machine:
    if args.config:
        if not args.machine:
            sys.exit("--config requires --machine")
        with open(args.config) as f:
            config = json.load(f)
        for raw in config.get("machines", []):
            mc = amt.MachineConfig(raw)
            if mc.name == args.machine:
                return amt.machine_from_config(mc)
        sys.exit(f"no machine {args.machine!r} in {args.config}")

    if not args.host:
        sys.exit("need --host (or --config + --machine)")
    password: Optional[str] = (
        args.password or os.environ.get("AMT_PASSWORD")
    )
    if not password:
        sys.exit("no password: pass --password or set $AMT_PASSWORD")
    mc = amt.MachineConfig({
        "name": args.host,
        "host": args.host,
        "user": args.user,
        "password": password,
        "tls": args.tls,
        "boot_method": args.boot_method,
        "skip_boot_source": args.skip_boot_source,
    })
    return amt.machine_from_config(mc)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Manual AMT power/boot control"
    )
    parser.add_argument("--host", help="AMT IP / DNS name")
    parser.add_argument("--user", default="admin")
    parser.add_argument("--password",
                        help="defaults to $AMT_PASSWORD")
    parser.add_argument("--tls", action="store_true")
    parser.add_argument("--boot-method",
                        choices=["wsman", "meshcmd"],
                        default="wsman")
    parser.add_argument("--skip-boot-source", action="store_true")
    parser.add_argument("--config",
                        help="server config JSON (with --machine)")
    parser.add_argument("--machine",
                        help="machine name from --config")
    parser.add_argument(
        "cmd",
        choices=["status", "on", "off", "reset", "boot-pxe", "mac"],
    )
    args = parser.parse_args()

    machine = machine_from_args(args)
    try:
        if args.cmd == "status":
            state = machine.power_state()
            label = POWER_LABELS.get(state, f"state {state}")
            print(f"{machine.config.host}: power = {label}")
        elif args.cmd == "on":
            assert isinstance(machine, amt.AmtMachine)
            machine.amt.set_power(amt.POWER_ON)
            print("powered on")
        elif args.cmd == "off":
            machine.power_off()
            print("powered off")
        elif args.cmd == "reset":
            assert isinstance(machine, amt.AmtMachine)
            machine.amt.set_power(amt.POWER_RESET)
            print("reset")
        elif args.cmd == "boot-pxe":
            machine.boot()
            print("PXE boot triggered (serve the boot files "
                  "yourself over DHCP/TFTP)")
        elif args.cmd == "mac":
            print(f"{machine.config.host}: mac = {machine.mac_address()}")
    except amt.AmtError as e:
        sys.exit(f"AMT error: {e}")


if __name__ == "__main__":
    main()
