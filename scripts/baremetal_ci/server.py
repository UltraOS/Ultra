#!/usr/bin/env python3
# The baremetal CI server: accepts kernel binaries over HTTP, installs
# them into the PXE/TFTP root, boots the configured AMT machines and
# captures their serial output, then decides a verdict per machine.
#
# Scheduling model: the TFTP root is a single shared resource (every
# machine fetches the same kernel path), so jobs are strictly
# serialized. Within a job all target machines boot in parallel, one
# worker thread per machine. The next job's binary is only installed
# once every run of the current job is terminal and powered off.
#
# A machine with a configured MAC is served its own directory under
# the TFTP root by dnsmasq (tftp-unique-root=mac), refreshed on every
# install: hard links to the kernel and loader binaries plus a loader
# config whose command line starts with the machine's console_cmdline.
# A machine without a MAC gets the root config, base_cmdline only.
#
# Run verdicts:
#   pass     - the success regex matched
#   fail     - a failure regex matched, or the machine rebooted on its
#              own (the PXE banner appeared twice)
#   timeout  - the deadline passed, or output stalled (with criteria)
#   done     - a collection run finished (run_for elapsed, or an
#              interactive hold was ended by the client)
#   infra    - AMT/SOL trouble or PXE never took: says nothing about
#              the kernel
#   canceled - canceled while it still had a verdict pending
#
# HTTP API (JSON responses unless noted):
#   GET  /                the web UI
#   GET  /api/state       machines (with their last finished run),
#                         recent jobs, the server defaults and the
#                         criteria.json deployed with the server, the
#                         web UI's form defaults
#   GET  /api/machines    machine configs, identity and power state
#   GET  /api/jobs        recent jobs, ?limit=N
#   GET  /api/jobs/<id>   one job with its per-machine runs
#   GET  /api/jobs/<id>/log/<machine>?offset=N&raw=0|1&full=0|1
#                         log bytes from the offset, the next offset
#                         and run state come back in the X-Next-Offset
#                         and X-Run-State headers. Cleaned of terminal
#                         control sequences and cut at the last
#                         complete line while the run is live, raw=1
#                         returns the bytes as captured, full=1 the
#                         whole log as a downloadable text file
#   POST /api/jobs        submit a job
#   POST /api/jobs/<id>/cancel
#   POST /api/jobs/<id>/rerun
#
# POST /api/jobs body fields: kernel_b64 (required, an x86-64 ELF
# binary, anything else such as an ISO is rejected), machines (a
# list of names, "all" or "default"), cmdline (extra
# arguments after the machine's console line and the base), success
# (regex), failures (list of
# regexes), timeout, run_for, run_for_after (regex the run_for
# clock anchors on instead of the first serial output, so firmware
# consoles spamming SOL before the payload do not start it),
# stall_timeout, meta (free-form, kept with the run). Every /api
# endpoint checks X-Auth-Token when an auth_token is configured.

import argparse
import base64
import copy
import hashlib
import hmac
import json
import os
import re
import shutil
import sys
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Dict, List, Optional, Tuple

try:
    from . import amt
except ImportError:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import amt  # type: ignore[import-not-found,no-redef]

TERMINAL_STATES = (
    "pass", "fail", "timeout", "done", "infra", "canceled",
    "interrupted",
)

# Aggregate severity, worst first: a real kernel failure dominates
VERDICT_SEVERITY = (
    "fail", "timeout", "infra", "canceled", "interrupted", "pass",
    "done",
)

DEFAULT_HYPER_CFG = """[ultra-x86_64]
protocol = ultra
higher-half-exclusive = true

binary:
    path = "/{kernel}"
    allocate-anywhere = true

page-table:
    levels = 5
    constraint = maximum

cmdline = "{cmdline}"
"""

# Lines the SOL clients themselves print (connection status etc.):
# these must not count as machine output, both for boot-took
# detection and for starting a collection run's clock. Covers
# meshcmd amtterm and classic amtterm.
DEFAULT_SOL_CHATTER = (
    r"^(Connecting|Connected|Started |Debug: |MeshCmd|amtterm: |"
    r"ipv4 |ipv6 |connected now|serial-over-lan|"
    r"Intel\(R\) AMT|\s*$)"
)

MAX_UPLOAD = 64 * 1024 * 1024
LOG_CHUNK = 256 * 1024

ELF_MAGIC = b"\x7fELF"
ELF_CLASS_64 = 2
ELF_DATA_LSB = 1
EM_X86_64 = 0x3E

# What a PXE client fetches besides the kernel, installed by deploy
LOADER_FILES = ("BOOTX64.EFI", "hyper.pxe")


class ConfigError(RuntimeError):
    pass


class ServerConfig:
    def __init__(self, raw: Dict[str, Any], path: str):
        listen = raw.get("listen", "0.0.0.0:8090")
        host, sep, port = listen.rpartition(":")
        if not sep:
            raise ConfigError(f"bad listen address {listen!r}")
        self.listen_host = host
        self.listen_port = int(port)

        self.data_dir = raw.get("data_dir", "")
        self.tftp_root = raw.get("tftp_root", "")
        if not self.data_dir or not self.tftp_root:
            raise ConfigError("data_dir and tftp_root are required")

        self.kernel_filename = raw.get(
            "kernel_filename", "kernel-x86_64"
        )
        self.write_hyper_cfg = bool(raw.get("write_hyper_cfg", True))
        # Fleet-wide arguments between a machine's console_cmdline and
        # the job's own, a job cannot opt out of them
        self.base_cmdline = raw.get("base_cmdline", "")
        self.auth_token = raw.get("auth_token") or None
        self.max_runs_kept = int(raw.get("max_runs_kept", 200))
        self.max_parallel = int(raw.get("max_parallel", 8))
        self.default_machine = raw.get("default_machine") or None
        self.reboot_marker = raw.get(
            "reboot_marker", "Booted via network (PXE)"
        )
        self.sol_chatter = raw.get("sol_chatter", DEFAULT_SOL_CHATTER)

        defaults = raw.get("defaults", {})
        self.default_timeout = float(defaults.get("timeout", 600))
        self.first_output_timeout = float(
            defaults.get("first_output_timeout", 60)
        )
        self.boot_retries = int(defaults.get("boot_retries", 2))
        self.stall_timeout = float(defaults.get("stall_timeout", 0))

        template_path = raw.get("hyper_cfg_template")
        if template_path:
            if not os.path.isabs(template_path):
                template_path = os.path.join(
                    os.path.dirname(os.path.abspath(path)),
                    template_path
                )
            with open(template_path) as f:
                self.hyper_cfg_template = f.read()
        else:
            self.hyper_cfg_template = DEFAULT_HYPER_CFG

        machines_raw = raw.get("machines", [])
        if not machines_raw:
            raise ConfigError("no machines configured")
        self.machines: Dict[str, amt.MachineConfig] = {}
        macs: Dict[str, str] = {}
        for m in machines_raw:
            mc = amt.MachineConfig(m)
            if mc.name in self.machines:
                raise ConfigError(f"duplicate machine {mc.name!r}")
            if mc.mac:
                if mc.mac in macs:
                    raise ConfigError(
                        f"machines {macs[mc.mac]!r} and {mc.name!r} "
                        f"share mac {mc.mac}"
                    )
                macs[mc.mac] = mc.name
            self.machines[mc.name] = mc

        if (self.default_machine is not None
                and self.default_machine not in self.machines):
            raise ConfigError(
                f"default_machine {self.default_machine!r} "
                "is not a configured machine"
            )


def load_config(path: str) -> ServerConfig:
    with open(path) as f:
        return ServerConfig(json.load(f), path)


def load_deployed_criteria() -> Dict[str, Any]:
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "criteria.json")
    try:
        with open(path) as f:
            raw = json.load(f)
    except (OSError, ValueError):
        return {}
    if not isinstance(raw, dict):
        return {}
    return {
        "success": str(raw.get("success") or ""),
        "failures": [str(x) for x in raw.get("failures") or []],
        "kernel_start": str(raw.get("kernel_start") or ""),
    }


def atomic_write(path: str, data: bytes) -> None:
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


def check_kernel(kernel: bytes) -> None:
    if len(kernel) < 20 or kernel[:4] != ELF_MAGIC:
        raise ValueError(
            "the kernel is not an ELF file (an ISO or a disk image?)"
        )
    machine = int.from_bytes(kernel[18:20], "little")
    if (kernel[4] != ELF_CLASS_64 or kernel[5] != ELF_DATA_LSB
            or machine != EM_X86_64):
        raise ValueError("the kernel is not an x86-64 ELF binary")


# New bytes of a serial log from pos, cut back to the last
# complete line unless final, so an escape sequence or a marker
# is never split between two reads.
def read_lines(path: str, pos: int, limit: Optional[int] = None,
               final: bool = False) -> Tuple[bytes, int]:
    try:
        with open(path, "rb") as f:
            f.seek(pos)
            data = f.read() if limit is None else f.read(limit)
    except FileNotFoundError:
        return b"", pos
    if not final:
        cut = data.rfind(b"\n")
        if cut < 0:
            return b"", pos
        data = data[:cut + 1]
    return data, pos + len(data)


# Point dst at src's inode, atomically for a reader of dst.
def link_replace(src: str, dst: str) -> None:
    tmp = dst + ".tmp"
    if os.path.lexists(tmp):
        os.unlink(tmp)
    os.link(src, tmp)
    os.replace(tmp, dst)


class Job:
    def __init__(self, job_id: str, job_dir: str,
                 data: Dict[str, Any]):
        self.id = job_id
        self.dir = job_dir
        self.data = data
        self.cancel = threading.Event()

    @property
    def params(self) -> Dict[str, Any]:
        params: Dict[str, Any] = self.data["params"]
        return params

    @property
    def runs(self) -> Dict[str, Dict[str, Any]]:
        runs: Dict[str, Dict[str, Any]] = self.data["runs"]
        return runs

    def is_interactive(self) -> bool:
        p = self.params
        return p["success"] is None and p["run_for"] is None

    def kernel_path(self) -> str:
        return os.path.join(self.dir, "kernel")

    def log_path(self, machine: str) -> str:
        return os.path.join(self.dir, f"serial-{machine}.log")

    # A deep copy taken under the server lock, so a handler can
    # serialize it after dropping the lock.
    def summary(self) -> Dict[str, Any]:
        return copy.deepcopy({
            "id": self.id,
            "created_at": self.data["created_at"],
            "state": self.data["state"],
            "verdict": self.data["verdict"],
            "meta": self.data["meta"],
            "params": self.params,
            "runs": self.runs,
            "events": self.data["events"],
        })


class Server:
    def __init__(self, config: ServerConfig):
        self.config = config
        self.lock = threading.Lock()
        self.wakeup = threading.Condition(self.lock)
        self.jobs: Dict[str, Job] = {}
        self.order: List[str] = []
        self.active: Optional[Job] = None
        self.power_cache: Dict[str, Dict[str, Any]] = {}
        self.chatter_re = re.compile(config.sol_chatter)
        self.criteria = load_deployed_criteria()

        self.runs_dir = os.path.join(config.data_dir, "runs")
        os.makedirs(self.runs_dir, exist_ok=True)
        os.makedirs(config.tftp_root, exist_ok=True)
        self.counter_path = os.path.join(config.data_dir, "counter")

        self.load_existing()

    def load_existing(self) -> None:
        for name in sorted(os.listdir(self.runs_dir)):
            job_dir = os.path.join(self.runs_dir, name)
            meta = os.path.join(job_dir, "job.json")
            try:
                with open(meta) as f:
                    data = json.load(f)
            except (OSError, ValueError):
                continue
            job = Job(name, job_dir, data)
            if data["state"] != "done":
                data["state"] = "done"
                if data["verdict"] is None:
                    data["verdict"] = "interrupted"
                for run in job.runs.values():
                    if run["state"] not in TERMINAL_STATES:
                        run["state"] = "interrupted"
                        run["verdict"] = "interrupted"
                self.save_job(job)
            self.jobs[name] = job
            self.order.append(name)

    def next_id(self) -> str:
        try:
            with open(self.counter_path) as f:
                value = int(f.read().strip() or "0")
        except OSError:
            value = 0
        value += 1
        atomic_write(self.counter_path, str(value).encode())
        return f"{value:06d}"

    def save_job(self, job: Job) -> None:
        blob = json.dumps(job.data, indent=1).encode()
        atomic_write(os.path.join(job.dir, "job.json"), blob)

    def prune(self) -> None:
        with self.lock:
            excess = len(self.order) - self.config.max_runs_kept
            victims = []
            for job_id in list(self.order):
                if excess <= 0:
                    break
                job = self.jobs[job_id]
                if job.data["state"] != "done":
                    continue
                victims.append(job)
                self.order.remove(job_id)
                del self.jobs[job_id]
                excess -= 1
        for job in victims:
            shutil.rmtree(job.dir, ignore_errors=True)

    def resolve_machines(self, wanted: Any) -> List[str]:
        if wanted in (None, "all"):
            return list(self.config.machines)
        if wanted == "default":
            if self.config.default_machine is not None:
                return [self.config.default_machine]
            return [next(iter(self.config.machines))]
        if not isinstance(wanted, list):
            raise ValueError("machines must be a list, all or default")
        if not wanted:
            raise ValueError("empty machine list")
        names: List[str] = []
        for name in [str(x) for x in wanted]:
            if name not in self.config.machines:
                raise ValueError(f"unknown machine {name!r}")
            if name not in names:
                names.append(name)
        return names

    def submit(self, payload: Dict[str, Any],
               kernel: Optional[bytes] = None) -> Job:
        cfg = self.config
        if not isinstance(payload, dict):
            raise ValueError("the job must be a JSON object")
        machines = self.resolve_machines(payload.get("machines"))

        if kernel is None:
            kernel_b64 = payload.get("kernel_b64")
            if not isinstance(kernel_b64, str) or not kernel_b64:
                raise ValueError("kernel_b64 is required")
            kernel = base64.b64decode(kernel_b64)
        check_kernel(kernel)

        cmdline = str(payload.get("cmdline") or "")
        if '"' in cmdline:
            raise ValueError("cmdline must not contain double quotes")

        success = payload.get("success")
        if success is not None:
            success = str(success)
            re.compile(success)
        failures_raw = payload.get("failures", [])
        if not isinstance(failures_raw, list):
            raise ValueError("failures must be a list of regexes")
        failures = [str(x) for x in failures_raw]
        for pattern in failures:
            re.compile(pattern)

        run_for = payload.get("run_for")
        if run_for is not None:
            run_for = float(run_for)
        run_for_after = payload.get("run_for_after")
        if run_for_after is not None:
            run_for_after = str(run_for_after)
            re.compile(run_for_after)
        stall_timeout = payload.get("stall_timeout")
        if stall_timeout is None:
            stall_timeout = cfg.stall_timeout

        params = {
            "cmdline": cmdline,
            "machines": machines,
            "success": success,
            "failures": failures,
            "timeout": float(
                payload.get("timeout") or cfg.default_timeout
            ),
            "run_for": run_for,
            "run_for_after": run_for_after,
            "stall_timeout": float(stall_timeout),
        }

        with self.lock:
            job_id = self.next_id()
            job_dir = os.path.join(self.runs_dir, job_id)
            os.makedirs(job_dir)
            data: Dict[str, Any] = {
                "id": job_id,
                "created_at": time.time(),
                "state": "queued",
                "verdict": None,
                "params": params,
                "meta": payload.get("meta") or {},
                "kernel_sha256": hashlib.sha256(kernel).hexdigest(),
                "runs": {
                    name: {"state": "queued", "verdict": None,
                           "detail": ""}
                    for name in machines
                },
                "events": [],
            }
            job = Job(job_id, job_dir, data)
            with open(job.kernel_path(), "wb") as f:
                f.write(kernel)
            self.save_job(job)
            self.jobs[job_id] = job
            self.order.append(job_id)
            self.wakeup.notify_all()
        return job

    def rerun(self, job_id: str) -> Job:
        with self.lock:
            old = self.jobs.get(job_id)
            if old is None:
                raise ValueError(f"no job {job_id}")
            payload = {
                **old.params,
                "meta": {**old.data["meta"],
                         "rerun_of": old.id},
            }
            with open(old.kernel_path(), "rb") as f:
                kernel = f.read()
        return self.submit(payload, kernel=kernel)

    def cancel_job(self, job_id: str) -> bool:
        with self.lock:
            job = self.jobs.get(job_id)
            if job is None:
                raise ValueError(f"no job {job_id}")
            if job.data["state"] == "done":
                return False
            if job.data["state"] == "queued":
                job.data["state"] = "done"
                job.data["verdict"] = "canceled"
                for run in job.runs.values():
                    run["state"] = "canceled"
                    run["verdict"] = "canceled"
                self.save_job(job)
                return True
            job.cancel.set()
            return True

    def busy_machines(self) -> List[str]:
        active = self.active
        if active is None or active.data["state"] == "done":
            return []
        return [
            name for name, run in active.runs.items()
            if run["state"] not in TERMINAL_STATES
        ]

    def poll_power_loop(self) -> None:
        while True:
            busy = self.busy_machines()
            for name, mc in self.config.machines.items():
                if name in busy:
                    continue
                entry: Dict[str, Any]
                try:
                    machine = amt.machine_from_config(mc)
                    entry = {"power": machine.power_state()}
                except (amt.AmtError, OSError) as e:
                    entry = {"power": None, "error": str(e)}
                entry["checked_at"] = time.time()
                with self.lock:
                    self.power_cache[name] = entry
            time.sleep(60)

    # The newest finished run of every machine, under the lock.
    def last_runs(self) -> Dict[str, Dict[str, Any]]:
        wanted = set(self.config.machines)
        out: Dict[str, Dict[str, Any]] = {}
        for job_id in reversed(self.order):
            job = self.jobs[job_id]
            for name, run in job.runs.items():
                if name in wanted and run["verdict"] is not None:
                    out[name] = {
                        "job": job_id,
                        "verdict": run["verdict"],
                        "finished_at": run.get("finished_at"),
                    }
                    wanted.discard(name)
            if not wanted:
                break
        return out

    def machines_view(self) -> List[Dict[str, Any]]:
        busy = self.busy_machines()
        out = []
        with self.lock:
            cache = dict(self.power_cache)
            last = self.last_runs()
        for name, mc in self.config.machines.items():
            entry = dict(mc.describe())
            entry["busy"] = name in busy
            entry["last_run"] = last.get(name)
            entry.update(cache.get(name, {}))
            out.append(entry)
        return out

    def note(self, job: Job, msg: str) -> None:
        line = f"[{time.strftime('%H:%M:%S')}] {msg}"
        print(f"{job.id}: {msg}", flush=True)
        with self.lock:
            job.data["events"].append(line)
            self.save_job(job)

    def set_run(self, job: Job, name: str, state: str,
                detail: Optional[str] = None, **extra: Any) -> None:
        with self.lock:
            run = job.runs[name]
            run["state"] = state
            if state in TERMINAL_STATES:
                run["verdict"] = state
                run["finished_at"] = time.time()
            if detail is not None:
                run["detail"] = detail
            run.update(extra)
            self.save_job(job)

    def write_hyper_cfg(self, directory: str,
                        cmdline_parts: List[str]) -> None:
        cfg = self.config
        text = cfg.hyper_cfg_template.format(
            kernel=cfg.kernel_filename,
            cmdline=" ".join(p for p in cmdline_parts if p),
        )
        atomic_write(os.path.join(directory, "hyper.cfg"), text.encode())

    def client_dir(self, mc: amt.MachineConfig) -> str:
        return os.path.join(self.config.tftp_root, mc.mac)

    # Bring every configured machine's own TFTP directory in
    # line with the root: dnsmasq serves the directory in place of
    # the root, so it must hold everything the client fetches.
    # Directories of MACs no longer configured are removed so a
    # machine that lost its entry falls back to the root.
    def refresh_client_dirs(self, extra_cmdline: str) -> None:
        cfg = self.config
        wanted = {mc.mac for mc in cfg.machines.values() if mc.mac}
        for name in os.listdir(cfg.tftp_root):
            path = os.path.join(cfg.tftp_root, name)
            if (amt.MAC_RE.match(name) and name not in wanted
                    and os.path.isdir(path)):
                shutil.rmtree(path)
        for mc in cfg.machines.values():
            if not mc.mac:
                continue
            mdir = self.client_dir(mc)
            os.makedirs(mdir, exist_ok=True)
            for name in (cfg.kernel_filename, *LOADER_FILES):
                src = os.path.join(cfg.tftp_root, name)
                if os.path.exists(src):
                    link_replace(src, os.path.join(mdir, name))
            if cfg.write_hyper_cfg:
                self.write_hyper_cfg(
                    mdir,
                    [mc.console_cmdline, cfg.base_cmdline, extra_cmdline],
                )

    def install_kernel(self, job: Job) -> None:
        cfg = self.config
        extra = job.params["cmdline"]
        dst = os.path.join(cfg.tftp_root, cfg.kernel_filename)
        with open(job.kernel_path(), "rb") as f:
            atomic_write(dst, f.read())
        if cfg.write_hyper_cfg:
            self.write_hyper_cfg(cfg.tftp_root, [cfg.base_cmdline, extra])
        self.refresh_client_dirs(extra)

    def scheduler_loop(self) -> None:
        while True:
            with self.lock:
                job = None
                while job is None:
                    for job_id in self.order:
                        candidate = self.jobs[job_id]
                        if candidate.data["state"] == "queued":
                            job = candidate
                            break
                    if job is None:
                        self.wakeup.wait()
                job.data["state"] = "active"
                self.active = job
                self.save_job(job)
            try:
                self.execute_job(job)
            except Exception as e:
                self.note(job, f"job error: {e!r}")
                with self.lock:
                    for run in job.runs.values():
                        if run["state"] not in TERMINAL_STATES:
                            run["state"] = "infra"
                            run["verdict"] = "infra"
                            run["detail"] = str(e)
            self.finalize_job(job)
            self.prune()

    def execute_job(self, job: Job) -> None:
        self.note(job, "installing kernel into the TFTP root")
        self.install_kernel(job)

        gate = threading.Semaphore(self.config.max_parallel)
        threads = []
        for name in job.params["machines"]:
            t = threading.Thread(
                target=self.execute_run, args=(job, name, gate),
                daemon=True,
            )
            t.start()
            threads.append(t)
        for t in threads:
            t.join()

    def finalize_job(self, job: Job) -> None:
        with self.lock:
            for name, run in job.runs.items():
                if run["state"] not in TERMINAL_STATES:
                    run["state"] = "infra"
                    run["verdict"] = "infra"
                    run["detail"] = "worker ended without a verdict"
            verdicts = [r["verdict"] for r in job.runs.values()]
            final = "done"
            for v in VERDICT_SEVERITY:
                if v in verdicts:
                    final = v
                    break
            job.data["verdict"] = final
            job.data["state"] = "done"
            self.active = None
            self.save_job(job)
        self.note(job, f"job finished: {final}")

    def execute_run(self, job: Job, name: str,
                    gate: threading.Semaphore) -> None:
        with gate:
            if job.cancel.is_set():
                self.set_run(job, name, "canceled")
                return
            mc = self.config.machines[name]

            def notify(msg: str) -> None:
                self.note(job, f"{name}: {msg}")

            machine = amt.machine_from_config(mc, notify)
            sol: Optional[amt.SolSession] = None
            try:
                self.set_run(job, name, "booting",
                             started_at=time.time())
                sol = machine.start_sol(job.log_path(name))
                time.sleep(1)
                machine.boot()
                self.watch_run(job, name, machine, sol)
            except (amt.AmtError, OSError) as e:
                notify(f"infra error: {e}")
                self.set_run(job, name, "infra", detail=str(e))
            except Exception as e:
                notify(f"worker error: {e!r}")
                self.set_run(job, name, "infra", detail=repr(e))
            finally:
                # Stop SOL before powering off: an open redirection
                # session can block the power transition.
                if sol is not None:
                    sol.stop()
                time.sleep(1)
                try:
                    if machine.is_on():
                        machine.power_off()
                except (amt.AmtError, OSError) as e:
                    notify(f"power-off failed: {e}")

    def has_serial_activity(self, text: str) -> bool:
        for line in text.splitlines():
            if not self.chatter_re.search(line):
                return True
        return False

    def watch_run(self, job: Job, name: str, machine: amt.Machine,
                  sol: amt.SolSession) -> None:
        params = job.params
        logfile = job.log_path(name)
        success = (re.compile(params["success"])
                   if params["success"] else None)
        failures = [re.compile(f) for f in params["failures"]]
        run_for = params["run_for"]
        # The run_for clock anchors on this marker rather than on
        # the first serial output: some firmware streams its whole
        # BIOS screen over SOL, so "first output" can be POST noise
        # long before the payload the runtime is meant to measure
        run_for_after = (re.compile(params["run_for_after"])
                         if params.get("run_for_after") else None)
        stall_timeout = params["stall_timeout"]
        interactive = job.is_interactive()
        cfg = self.config

        deadline = time.time() + params["timeout"]
        pos = 0
        match_buf = ""
        banner_count = 0
        first_output: Optional[float] = None
        run_for_anchor: Optional[float] = None
        last_data = time.time()
        boot_time = time.time()
        retries = cfg.boot_retries

        while True:
            now = time.time()
            if job.cancel.is_set():
                verdict = "done" if interactive else "canceled"
                self.set_run(job, name, verdict)
                return
            if now >= deadline:
                if first_output is None:
                    self.set_run(job, name, "infra",
                                 detail="no serial output at all")
                elif interactive:
                    self.set_run(job, name, "done",
                                 detail="hold timeout")
                elif (run_for_after is not None
                        and run_for_anchor is None):
                    self.set_run(
                        job, name, "timeout",
                        detail="run_for_after marker never matched"
                    )
                else:
                    self.set_run(job, name, "timeout")
                return

            data, pos = read_lines(logfile, pos)
            if data:
                text = amt.clean_serial(data).decode(
                    "utf-8", "replace"
                )
                last_data = now
                if (first_output is None
                        and self.has_serial_activity(text)):
                    first_output = now
                    self.set_run(job, name, "running",
                                 first_output_at=now)
                # The whole read is searched together with the
                # retained buffer and only then trimmed, so a marker
                # inside a burst larger than the buffer is not lost
                scan = match_buf + text
                match_buf = scan[-8192:]
                if (run_for_after is not None
                        and run_for_anchor is None
                        and run_for_after.search(scan)):
                    run_for_anchor = now
                banner_count += text.count(cfg.reboot_marker)
                if banner_count >= 2:
                    self.set_run(
                        job, name, "fail",
                        detail="machine rebooted on its own"
                    )
                    return
                for f_re in failures:
                    m = f_re.search(scan)
                    if m:
                        self.set_run(
                            job, name, "fail",
                            detail=f"matched failure: {m.group(0)}"
                        )
                        return
                if success is not None:
                    m = success.search(scan)
                    if m:
                        self.set_run(
                            job, name, "pass",
                            detail=f"matched: {m.group(0)}"
                        )
                        return
            elif first_output is None:
                # Nothing will ever arrive without a capture client,
                # infra at once rather than after the full timeout
                if sol.poll() is not None:
                    self.set_run(
                        job, name, "infra",
                        detail=f"SOL client exited (rc="
                               f"{sol.returncode}) before any output"
                    )
                    return
                if now - boot_time >= cfg.first_output_timeout:
                    if retries == 0:
                        self.set_run(
                            job, name, "infra",
                            detail="no serial output after "
                                   f"{cfg.boot_retries + 1} boot "
                                   "attempts"
                        )
                        return
                    retries -= 1
                    self.note(
                        job,
                        f"{name}: no serial "
                        f"{int(now - boot_time)}s after boot, "
                        f"re-triggering ({retries} left)"
                    )
                    try:
                        machine.boot()
                    except (amt.AmtError, OSError) as e:
                        self.set_run(job, name, "infra",
                                     detail=f"re-boot failed: {e}")
                        return
                    boot_time = time.time()
                    banner_count = 0
                    match_buf = ""
            elif sol.poll() is not None:
                self.set_run(
                    job, name, "infra",
                    detail=f"SOL client exited (rc={sol.returncode}) "
                           "mid-run"
                )
                return

            if first_output is not None:
                anchor = (run_for_anchor if run_for_after is not None
                          else first_output)
                if (run_for is not None and anchor is not None
                        and now - anchor >= run_for):
                    self.set_run(job, name, "done",
                                 detail=f"ran for {run_for}s")
                    return
                if (stall_timeout and not interactive
                        and run_for is None
                        and now - last_data >= stall_timeout):
                    self.set_run(
                        job, name, "timeout",
                        detail=f"no output for {stall_timeout}s"
                    )
                    return

            time.sleep(0.5)


class CiHttpServer(ThreadingHTTPServer):
    daemon_threads = True
    ci: Server


class Handler(BaseHTTPRequestHandler):
    server: CiHttpServer  # type: ignore[assignment]

    def log_message(self, format: str, *args: Any) -> None:
        pass

    @property
    def ci(self) -> Server:
        return self.server.ci

    def _json(self, code: int, obj: Any) -> None:
        blob = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(blob)))
        self.end_headers()
        self.wfile.write(blob)

    def _error(self, code: int, msg: str) -> None:
        self._json(code, {"error": msg})

    def _authed(self) -> bool:
        token = self.ci.config.auth_token
        if token is None:
            return True
        given = self.headers.get("X-Auth-Token", "")
        if hmac.compare_digest(given.encode(), token.encode()):
            return True
        self._error(403, "bad or missing X-Auth-Token")
        return False

    def _read_body(self) -> Optional[bytes]:
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            length = 0
        if length <= 0:
            self._error(400, "missing request body")
            return None
        if length > MAX_UPLOAD:
            self._error(413, "request too large")
            return None
        return self.rfile.read(length)

    def do_GET(self) -> None:
        parsed = urllib.parse.urlsplit(self.path)
        path = parsed.path
        query = urllib.parse.parse_qs(parsed.query)

        if path == "/":
            self._serve_ui()
            return
        if not path.startswith("/api/"):
            self._error(404, "not found")
            return
        if not self._authed():
            return
        try:
            self._api_get(path, query)
        except ValueError as e:
            self._error(400, str(e))

    def _api_get(self, path: str, query: Dict[str, List[str]]) -> None:
        if path == "/api/state":
            self._api_state(query)
        elif path == "/api/machines":
            self._json(200, {"machines": self.ci.machines_view()})
        elif path == "/api/jobs":
            self._api_jobs(query)
        else:
            m = re.fullmatch(r"/api/jobs/([0-9]+)", path)
            if m:
                self._api_job(m.group(1))
                return
            m = re.fullmatch(
                r"/api/jobs/([0-9]+)/log/([\w.-]+)", path
            )
            if m:
                self._api_log(m.group(1), m.group(2), query)
                return
            self._error(404, "not found")

    def do_POST(self) -> None:
        path = urllib.parse.urlsplit(self.path).path
        if not self._authed():
            return

        if path == "/api/jobs":
            self._api_submit()
            return
        m = re.fullmatch(r"/api/jobs/([0-9]+)/cancel", path)
        if m:
            self._api_cancel(m.group(1))
            return
        m = re.fullmatch(r"/api/jobs/([0-9]+)/rerun", path)
        if m:
            self._api_rerun(m.group(1))
            return
        self._error(404, "not found")

    def _serve_ui(self) -> None:
        ui = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "webui.html")
        try:
            with open(ui, "rb") as f:
                blob = f.read()
        except OSError:
            self._error(500, "webui.html not found")
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(blob)))
        self.end_headers()
        self.wfile.write(blob)

    def _api_state(self, query: Dict[str, List[str]]) -> None:
        limit = int(query.get("limit", ["30"])[0])
        ci = self.ci
        with ci.lock:
            ids = list(reversed(ci.order))[:limit]
            jobs = [ci.jobs[i].summary() for i in ids]
        self._json(200, {
            "machines": ci.machines_view(),
            "jobs": jobs,
            "default_machine": ci.config.default_machine,
            "default_timeout": ci.config.default_timeout,
            "criteria": ci.criteria,
        })

    def _api_jobs(self, query: Dict[str, List[str]]) -> None:
        limit = int(query.get("limit", ["50"])[0])
        ci = self.ci
        with ci.lock:
            ids = list(reversed(ci.order))[:limit]
            jobs = [ci.jobs[i].summary() for i in ids]
        self._json(200, {"jobs": jobs})

    def _api_job(self, job_id: str) -> None:
        with self.ci.lock:
            job = self.ci.jobs.get(job_id)
            summary = job.summary() if job is not None else None
        if summary is None:
            self._error(404, f"no job {job_id}")
            return
        self._json(200, summary)

    def _api_log(self, job_id: str, machine: str,
                 query: Dict[str, List[str]]) -> None:
        with self.ci.lock:
            job = self.ci.jobs.get(job_id)
            if job is None or machine not in job.runs:
                self._error(404, "no such run")
                return
            state = job.runs[machine]["state"]
            path = job.log_path(machine)
        offset = int(query.get("offset", ["0"])[0])
        raw = query.get("raw", ["0"])[0] == "1"
        full = query.get("full", ["0"])[0] == "1"
        final = raw or full or state in TERMINAL_STATES
        limit = None if full else LOG_CHUNK
        data, offset = read_lines(path, offset, limit, final)
        if not raw:
            data = amt.clean_serial(data)
        self.send_response(200)
        if full:
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header(
                "Content-Disposition",
                f'attachment; filename="{job_id}-{machine}.log"'
            )
        else:
            self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("X-Next-Offset", str(offset))
        self.send_header("X-Run-State", state)
        self.end_headers()
        self.wfile.write(data)

    def _api_submit(self) -> None:
        body = self._read_body()
        if body is None:
            return
        try:
            payload = json.loads(body)
            job = self.ci.submit(payload)
        except (ValueError, re.error) as e:
            self._error(400, str(e))
            return
        self._json(200, {"id": job.id})

    def _api_cancel(self, job_id: str) -> None:
        try:
            changed = self.ci.cancel_job(job_id)
        except ValueError as e:
            self._error(404, str(e))
            return
        self._json(200, {"canceled": changed})

    def _api_rerun(self, job_id: str) -> None:
        try:
            job = self.ci.rerun(job_id)
        except ValueError as e:
            self._error(404, str(e))
            return
        self._json(200, {"id": job.id})


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Ultra baremetal CI server"
    )
    parser.add_argument("--config", required=True,
                        help="Path to the server config JSON")
    args = parser.parse_args()

    try:
        config = load_config(args.config)
    except (ConfigError, ValueError, OSError) as e:
        sys.exit(f"config error: {e}")

    ci = Server(config)
    threading.Thread(target=ci.scheduler_loop, daemon=True).start()
    threading.Thread(target=ci.poll_power_loop, daemon=True).start()

    httpd = CiHttpServer(
        (config.listen_host, config.listen_port), Handler
    )
    httpd.ci = ci
    print(f"listening on {config.listen_host}:{config.listen_port}, "
          f"{len(config.machines)} machine(s)", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
