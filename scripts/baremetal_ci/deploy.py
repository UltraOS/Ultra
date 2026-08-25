#!/usr/bin/env python3

import argparse
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
from typing import Any, Dict, List, Optional

STAGING = ".ultra-ci-staging"
QUEUE_DRAIN_TIMEOUT = 20 * 60

# Filled into deploy/install.sh before it is sent to the target
PLACEHOLDERS = ("@STAGING@", "@SUBNET@")


def _run(cmd: List[str]) -> None:
    r = subprocess.run(cmd)
    if r.returncode != 0:
        raise RuntimeError(f"'{cmd[0]}' failed with {r.returncode}")


def read_config(path: str) -> Dict[str, Any]:
    with open(path) as f:
        raw = json.load(f)
    if not isinstance(raw, dict):
        raise RuntimeError(f"{path}: not a JSON object")
    subnet = raw.get("pxe_subnet")
    if not isinstance(subnet, str) or not re.fullmatch(
            r"\d+\.\d+\.\d+\.\d+", subnet):
        raise RuntimeError(
            f"{path}: pxe_subnet must name the subnet dnsmasq "
            "answers PXE clients on, e.g. 192.168.1.0"
        )
    return raw


# Wait until the server already running on the target has
# nothing active or queued, a restart interrupts whatever it is
# running. A server that does not answer (first deploy, service
# down) is nothing to wait for.
def wait_queue_drained(target: str, raw: Dict[str, Any],
                       timeout: float) -> None:
    host = target.rsplit("@", 1)[-1]
    port = str(raw.get("listen", "0.0.0.0:8090")).rpartition(":")[2]
    headers = {}
    if raw.get("auth_token"):
        headers["X-Auth-Token"] = str(raw["auth_token"])
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    url = f"http://{host}:{port}/api/jobs?limit=50"
    deadline = time.monotonic() + timeout
    reported: List[str] = []

    while True:
        req = urllib.request.Request(url, headers=headers)
        try:
            with opener.open(req, timeout=10) as resp:
                jobs = json.load(resp)["jobs"]
        except (urllib.error.URLError, OSError, ValueError, KeyError):
            return
        pending = [j["id"] for j in jobs if j["state"] != "done"]
        if not pending:
            return
        if pending != reported:
            reported = pending
            print(f"deploy: waiting for {', '.join(pending)} to "
                  "finish before restarting the server", flush=True)
        if time.monotonic() >= deadline:
            raise RuntimeError(
                f"{', '.join(pending)} still not done after "
                f"{int(timeout)}s, cancel or wait, then deploy again"
            )
        time.sleep(5)


def deploy(target: str, config: str,
           bootx64: Optional[str] = None,
           pxe: Optional[str] = None) -> int:
    pkg_dir = os.path.dirname(os.path.abspath(__file__))
    deploy_dir = os.path.join(pkg_dir, "deploy")

    try:
        raw = read_config(config)
        wait_queue_drained(target, raw, QUEUE_DRAIN_TIMEOUT)
        with open(os.path.join(deploy_dir, "install.sh")) as f:
            script = f.read()
        for placeholder, value in zip(PLACEHOLDERS,
                                      (STAGING, raw["pxe_subnet"])):
            script = script.replace(placeholder, value)
        _run(["ssh", target, f"mkdir -p {STAGING}"])
        _run(["rsync", "-r", "--delete", "--exclude", "deploy",
              "--exclude", "__pycache__", pkg_dir + "/",
              f"{target}:{STAGING}/baremetal_ci/"])
        _run(["rsync",
              os.path.join(deploy_dir, "ultra-ci.service"),
              os.path.join(deploy_dir, "ultra-ci-dnsmasq.service"),
              os.path.join(deploy_dir, "dnsmasq.conf"),
              f"{target}:{STAGING}/"])
        _run(["rsync", config, f"{target}:{STAGING}/config.json"])
        if bootx64:
            _run(["rsync", bootx64,
                  f"{target}:{STAGING}/BOOTX64.EFI"])
        if pxe:
            _run(["rsync", pxe, f"{target}:{STAGING}/hyper.pxe"])
        # -t so the remote sudo can prompt for its password
        _run(["ssh", "-t", target, script])
    except (RuntimeError, OSError) as e:
        print(f"deploy: {e}", file=sys.stderr)
        return 1
    return 0


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Deploy the baremetal CI runner"
    )
    parser.add_argument("target", help="[user@]host to deploy to")
    parser.add_argument("config",
                        help="server config to install as the "
                             "live /srv/ultra-ci/config.json")
    parser.add_argument("--bootx64",
                        help="hyper UEFI loader to install into "
                             "the TFTP root")
    parser.add_argument("--pxe",
                        help="hyper BIOS PXE loader to install "
                             "into the TFTP root (as hyper.pxe)")
    args = parser.parse_args()
    sys.exit(deploy(args.target, args.config, args.bootx64,
                    args.pxe))


if __name__ == "__main__":
    main()
