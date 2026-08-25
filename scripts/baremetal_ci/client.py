#!/usr/bin/env python3

import argparse
import base64
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Any, Callable, Dict, List, Optional, Tuple

EXIT_CODES = {
    "pass": 0,
    "done": 0,
    "fail": 1,
    "timeout": 2,
    "infra": 3,
    "interrupted": 3,
    "canceled": 4,
}


class CiClientError(RuntimeError):
    pass


class CiClient:
    def __init__(self, server: str, token: Optional[str] = None):
        if "//" not in server:
            server = "http://" + server
        parsed = urllib.parse.urlsplit(server)
        netloc = parsed.netloc
        if ":" not in netloc:
            netloc += ":8090"
        self.base = f"{parsed.scheme}://{netloc}"
        self.token = token or os.environ.get("ULTRA_CI_TOKEN")
        self.opener = urllib.request.build_opener(
            urllib.request.ProxyHandler({})
        )

    def _request(self, path: str,
                 body: Optional[Dict[str, Any]] = None,
                 method: Optional[str] = None) -> Any:
        data = None
        headers = {}
        if body is not None:
            data = json.dumps(body).encode()
            headers["Content-Type"] = "application/json"
        if self.token:
            headers["X-Auth-Token"] = self.token
        req = urllib.request.Request(
            self.base + path, data=data, headers=headers,
            method=method,
        )
        try:
            with self.opener.open(req, timeout=60) as resp:
                return json.loads(resp.read().decode())
        except urllib.error.HTTPError as e:
            try:
                msg = json.loads(e.read().decode())["error"]
            except (ValueError, KeyError):
                msg = f"HTTP {e.code}"
            raise CiClientError(f"{path}: {msg}")
        except (urllib.error.URLError, OSError) as e:
            raise CiClientError(f"CI server unreachable: {e}")

    def read_log(self, job_id: str, machine: str,
                 offset: int) -> Tuple[bytes, int, str]:
        path = (f"/api/jobs/{job_id}/log/{machine}"
                f"?offset={offset}")
        headers = {}
        if self.token:
            headers["X-Auth-Token"] = self.token
        req = urllib.request.Request(self.base + path,
                                     headers=headers)
        try:
            with self.opener.open(req, timeout=60) as resp:
                data = resp.read()
                next_offset = int(
                    resp.headers.get("X-Next-Offset", offset)
                )
                state = resp.headers.get("X-Run-State", "")
                return data, next_offset, state
        except (urllib.error.URLError, OSError) as e:
            raise CiClientError(f"CI server unreachable: {e}")

    def read_log_full(self, job_id: str, machine: str) -> bytes:
        path = f"/api/jobs/{job_id}/log/{machine}?full=1"
        headers = {}
        if self.token:
            headers["X-Auth-Token"] = self.token
        req = urllib.request.Request(self.base + path,
                                     headers=headers)
        try:
            with self.opener.open(req, timeout=60) as resp:
                data: bytes = resp.read()
                return data
        except (urllib.error.URLError, OSError) as e:
            raise CiClientError(f"CI server unreachable: {e}")

    def submit(self, payload: Dict[str, Any]) -> str:
        job_id = self._request("/api/jobs", body=payload)["id"]
        return str(job_id)

    def job(self, job_id: str) -> Dict[str, Any]:
        return self._request(f"/api/jobs/{job_id}")

    def cancel(self, job_id: str) -> None:
        self._request(f"/api/jobs/{job_id}/cancel", method="POST")

    def state(self) -> Dict[str, Any]:
        return self._request("/api/state")


def load_default_criteria() -> Dict[str, Any]:
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "criteria.json")
    with open(path) as f:
        criteria: Dict[str, Any] = json.load(f)
    return criteria


def git_meta() -> Dict[str, str]:
    meta = {}
    queries = {
        "git_describe": ["git", "describe", "--always", "--dirty"],
        "branch": ["git", "rev-parse", "--abbrev-ref", "HEAD"],
    }
    for key, cmd in queries.items():
        try:
            r = subprocess.run(cmd, stdout=subprocess.PIPE,
                               stderr=subprocess.DEVNULL)
            if r.returncode == 0:
                meta[key] = r.stdout.decode().strip()
        except OSError:
            pass
    return meta


def add_run_args(target: argparse._ActionsContainer, prefix: str = "",
                 run_for: bool = True, log_dir: bool = True) -> None:
    target.add_argument(f"--{prefix}cmdline", dest="cmdline",
                        metavar="ARGS",
                        help="Extra kernel command line, appended "
                             "after the machine's console line and "
                             "the server's base")
    target.add_argument(f"--{prefix}timeout", dest="timeout",
                        type=float, metavar="SECONDS",
                        help="Give up on a run after this long, the "
                             "server's default otherwise")
    target.add_argument(f"--{prefix}no-wait", dest="no_wait",
                        action="store_true",
                        help="Submit the job and return without "
                             "waiting for it")
    if run_for:
        target.add_argument("--run-for", type=float, metavar="SECONDS",
                            help="Collect the log for this long after "
                                 "the kernel starts and power off, "
                                 "instead of judging by the criteria")
    if log_dir:
        target.add_argument("--log-dir", metavar="DIR",
                            help="Save every run's log under DIR/<job>/")


def add_build_args(group: argparse._ActionsContainer) -> None:
    group.add_argument("--baremetal", dest="machines", metavar="MACHINES",
                       nargs="?", const="default",
                       help="Run on bare metal. Default machine, 'all', or a "
                            "comma-separated list")
    group.add_argument("--baremetal-server", dest="server", metavar="HOST",
                       default=os.environ.get("ULTRA_CI_SERVER"),
                       help="CI server host[:port], defaults to "
                            "$ULTRA_CI_SERVER")
    group.add_argument("--baremetal-machines", action="store_true",
                       help="List the machines the server offers, then "
                            "exit")
    group.add_argument("--baremetal-deploy", nargs=2,
                       metavar=("HOST", "CONFIG"),
                       help="Install or update the CI server on HOST "
                            "with the server CONFIG")
    add_run_args(group, prefix="baremetal-", run_for=False, log_dir=False)


def require_server(args: argparse.Namespace,
                   flag: str = "--baremetal-server") -> str:
    if not args.server:
        sys.exit(f"no CI server: pass {flag} or set $ULTRA_CI_SERVER")
    server: str = args.server
    return server


def early_action(
    args: argparse.Namespace, loaders: Callable[[], Tuple[str, str]]
) -> Optional[int]:
    if args.baremetal_deploy:
        from . import deploy
        bootx64, pxe = loaders()
        host, config = args.baremetal_deploy
        return deploy.deploy(host, config, bootx64=bootx64, pxe=pxe)
    if args.baremetal_machines:
        return list_machines(require_server(args))
    return None


def run_from_args(args: argparse.Namespace, arch: str, kernel_path: str,
                  build_dir: str) -> Optional[int]:
    if args.machines is None:
        return None
    if arch != "x86_64":
        sys.exit("--baremetal only supports x86_64")
    return submit_and_watch(
        require_server(args), payload_from_args(args, kernel_path),
        no_wait=args.no_wait, log_dir=os.path.join(build_dir, "baremetal"),
    )


def payload_from_args(args: argparse.Namespace,
                      kernel_path: str) -> Dict[str, Any]:
    return build_payload(kernel_path, args.run_for is None, args.machines,
                         args.run_for, args.timeout, args.cmdline)


def build_payload(kernel_path: str, ci: bool,
                  machines: Optional[str],
                  run_for: Optional[float],
                  timeout: Optional[float],
                  cmdline: Optional[str]) -> Dict[str, Any]:
    with open(kernel_path, "rb") as f:
        kernel = f.read()

    payload: Dict[str, Any] = {
        "kernel_b64": base64.b64encode(kernel).decode(),
        "meta": git_meta(),
    }
    if machines in (None, "all", "default"):
        payload["machines"] = machines or ("all" if ci else "default")
    else:
        assert machines is not None
        payload["machines"] = [
            m.strip() for m in machines.split(",") if m.strip()
        ]
    if ci:
        criteria = load_default_criteria()
        payload["success"] = criteria["success"]
        payload["failures"] = criteria.get("failures", [])
    elif run_for is not None:
        payload["run_for"] = run_for
        marker = load_default_criteria().get("kernel_start")
        if marker:
            payload["run_for_after"] = marker
    if timeout is not None:
        payload["timeout"] = timeout
    if cmdline is not None:
        payload["cmdline"] = cmdline
    return payload


# Line-buffers a machine's log stream and prints it, with a
# [machine] prefix when more than one machine is watched.
class LogMux:
    def __init__(self, machines: List[str]):
        self.offsets = {m: 0 for m in machines}
        self.partial = {m: "" for m in machines}
        self.prefix = len(machines) > 1

    def pump(self, client: CiClient, job_id: str) -> None:
        for name in self.offsets:
            while True:
                data, next_offset, _ = client.read_log(
                    job_id, name, self.offsets[name]
                )
                if next_offset == self.offsets[name]:
                    break
                self.offsets[name] = next_offset
                self._emit(name, data.decode("utf-8", "replace"))

    def _emit(self, name: str, text: str) -> None:
        buf = self.partial[name] + text
        lines = buf.split("\n")
        self.partial[name] = lines.pop()
        for line in lines:
            if self.prefix:
                print(f"[{name}] {line}")
            else:
                print(line)
        sys.stdout.flush()

    def flush(self) -> None:
        for name, rest in self.partial.items():
            if rest:
                self._emit(name, "\n")


TAIL_LINES = 15


# Fetch every run's full log into log_dir/<job>/<machine>.log
# and return the paths.
def save_logs(client: CiClient, job_id: str,
              machines: List[str], log_dir: str) -> Dict[str, str]:
    job_dir = os.path.join(log_dir, job_id)
    os.makedirs(job_dir, exist_ok=True)
    paths = {}
    for name in machines:
        path = os.path.join(job_dir, f"{name}.log")
        with open(path, "wb") as f:
            f.write(client.read_log_full(job_id, name))
        paths[name] = path
    return paths


def print_tail(client: CiClient, job_id: str, name: str) -> None:
    text = client.read_log_full(job_id, name).decode("utf-8", "replace")
    lines = text.splitlines()
    if not lines:
        print(f"[ci] --- {name}: no output")
        return
    shown = lines[-TAIL_LINES:]
    print(f"[ci] --- {name}: last {len(shown)} of {len(lines)} lines")
    for line in shown:
        print(f"[{name}] {line}")


# Follow a job to its end and print the verdict. A single
# machine's serial log is streamed live, several machines' logs
# are not interleaved: their state changes are printed as they
# happen, the logs are saved to log_dir and the tail of every
# run that did not pass is printed at the end.
def watch(client: CiClient, job_id: str,
          log_dir: Optional[str] = None) -> int:
    job = client.job(job_id)
    machines = list(job["runs"])
    stream = len(machines) == 1
    mux = LogMux(machines)
    states = {m: "" for m in machines}
    print(f"[ci] job {job_id} on {', '.join(machines)}, "
          f"{client.base}/#job/{job_id}")

    try:
        while True:
            job = client.job(job_id)
            # Logs first, so a verdict line follows the output that
            # caused it
            if stream:
                mux.pump(client, job_id)
            for name, run in job["runs"].items():
                st = run["verdict"] or run["state"]
                if st != states[name]:
                    states[name] = st
                    detail = run.get("detail") or ""
                    if detail:
                        detail = f" ({detail})"
                    print(f"[ci] {name}: {st}{detail}")
            if job["state"] == "done":
                break
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n[ci] interrupt: ending the job", file=sys.stderr)
        try:
            client.cancel(job_id)
            while client.job(job_id)["state"] != "done":
                time.sleep(1)
            job = client.job(job_id)
        except CiClientError as e:
            print(f"[ci] cancel failed: {e}", file=sys.stderr)
            return EXIT_CODES["canceled"]

    try:
        if stream:
            mux.pump(client, job_id)
            mux.flush()
        else:
            for name, run in job["runs"].items():
                if run["verdict"] not in ("pass", "done"):
                    print_tail(client, job_id, name)
        if log_dir is not None:
            save_logs(client, job_id, machines, log_dir)
            print(f"[ci] logs saved under "
                  f"{os.path.join(log_dir, job_id)}")
    except CiClientError as e:
        print(f"[ci] {e}", file=sys.stderr)
    verdict = job["verdict"]
    print(f"[ci] job {job_id} finished: {verdict}")
    for name, run in job["runs"].items():
        detail = run.get("detail") or ""
        if detail:
            detail = f" ({detail})"
        print(f"[ci]   {name}: {run['verdict']}{detail}")
    return EXIT_CODES.get(verdict, 3)


def list_machines(server: str) -> int:
    try:
        client = CiClient(server)
        rows = []
        for m in client.state()["machines"]:
            power = {2: "on"}.get(m.get("power"), "off")
            if m.get("power") is None:
                power = "unreachable"
            if "checked_at" not in m:
                power = "pending"
            if m.get("busy"):
                power = "busy"
            rows.append((m["name"], m.get("host") or "", power,
                         m.get("description") or ""))
        widths = [max(len(r[i]) for r in rows) for i in range(3)]
        for row in rows:
            print(f"{row[0]:<{widths[0]}}  {row[1]:<{widths[1]}}  "
                  f"{row[2]:<{widths[2]}}  {row[3]}".rstrip())
    except CiClientError as e:
        print(f"[ci] {e}", file=sys.stderr)
        return 3
    return 0


def submit_and_watch(server: str, payload: Dict[str, Any],
                     no_wait: bool = False,
                     log_dir: Optional[str] = None) -> int:
    try:
        client = CiClient(server)
        job_id = client.submit(payload)
        if no_wait:
            print(f"[ci] submitted job {job_id} (not waiting), "
                  f"{client.base}/#job/{job_id}")
            return 0
        return watch(client, job_id, log_dir)
    except CiClientError as e:
        print(f"[ci] {e}", file=sys.stderr)
        return 3


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Ultra baremetal CI client"
    )
    parser.add_argument("--server", metavar="HOST",
                        default=os.environ.get("ULTRA_CI_SERVER"),
                        help="CI server host[:port] or URL, defaults "
                             "to $ULTRA_CI_SERVER")
    parser.add_argument("--token",
                        help="Auth token, defaults to $ULTRA_CI_TOKEN")
    sub = parser.add_subparsers(dest="cmd", required=True)

    r = sub.add_parser("run", help="submit a kernel and watch it")
    r.add_argument("--kernel", required=True,
                   help="Path to the kernel binary")
    r.add_argument("--machines", metavar="MACHINES",
                   help="A machine (by name), a comma-separated "
                        "set, or all, defaults to the server's default "
                        "machine")
    add_run_args(r)

    w = sub.add_parser("watch", help="attach to an existing job")
    w.add_argument("job_id")
    w.add_argument("--log-dir", metavar="DIR",
                   help="Save every run's log under DIR/<job>/")

    sub.add_parser("list", help="list recent jobs")
    sub.add_parser("machines", help="list machines and their state")

    c = sub.add_parser("cancel", help="cancel a job")
    c.add_argument("job_id")

    args = parser.parse_args()
    if args.token:
        os.environ["ULTRA_CI_TOKEN"] = args.token
    server = require_server(args, "--server")
    client = CiClient(server, token=args.token)

    try:
        if args.cmd == "run":
            sys.exit(submit_and_watch(
                server, payload_from_args(args, args.kernel),
                args.no_wait, args.log_dir,
            ))
        elif args.cmd == "watch":
            sys.exit(watch(client, args.job_id, args.log_dir))
        elif args.cmd == "list":
            for job in client.state()["jobs"]:
                verdict = job["verdict"] or job["state"]
                created = time.strftime(
                    "%Y-%m-%d %H:%M:%S",
                    time.localtime(job["created_at"]),
                )
                meta = job["meta"].get("git_describe", "")
                print(f"{job['id']}  {created}  {verdict:<11} {meta}")
        elif args.cmd == "machines":
            sys.exit(list_machines(server))
        elif args.cmd == "cancel":
            client.cancel(args.job_id)
            print(f"canceled {args.job_id}")
    except CiClientError as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
