# Intel AMT machine control for the baremetal CI: WS-MAN power/boot
# management, Serial-over-LAN capture, and the per-machine quirk
# dispatch (boot method, SOL client) driven by configuration.

import os
import pty
import re
import shutil
import ssl
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
import uuid
import xml.etree.ElementTree as ET
from typing import Callable, Dict, List, Optional

NS = {
    "s": "http://www.w3.org/2003/05/soap-envelope",
    "a": "http://schemas.xmlsoap.org/ws/2004/08/addressing",
    "w": "http://schemas.dmtf.org/wbem/wsman/1/wsman.xsd",
}
ANON = "http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous"

_CIM = "http://schemas.dmtf.org/wbem/wscim/1/cim-schema/2/"
URI_BOOT_SETTING = (
    "http://intel.com/wbem/wscim/1/amt-schema/1/AMT_BootSettingData"
)
URI_BOOT_CONFIG = _CIM + "CIM_BootConfigSetting"
URI_BOOT_SOURCE = _CIM + "CIM_BootSourceSetting"
URI_BOOT_SERVICE = _CIM + "CIM_BootService"
URI_POWER_SERVICE = _CIM + "CIM_PowerManagementService"
URI_ASSOC_POWER = _CIM + "CIM_AssociatedPowerManagementService"
URI_COMPUTER = _CIM + "CIM_ComputerSystem"
URI_ETHERNET_PORT = (
    "http://intel.com/wbem/wscim/1/amt-schema/1/AMT_EthernetPortSettings"
)

INST_BOOT_SETTING = "Intel(r) AMT:BootSettingData 0"
INST_BOOT_CONFIG = "Intel(r) AMT: Boot Configuration 0"
INST_BOOT_PXE = "Intel(r) AMT: Force PXE Boot"
NAME_BOOT_SERVICE = "Intel(r) AMT Boot Service"
NAME_POWER_SERVICE = "Intel(r) AMT Power Management Service"
# The wired port, shared between the ME and the host
INST_ETHERNET_WIRED = "Intel(r) AMT Ethernet Port Settings 0"

ACTION_GET = "http://schemas.xmlsoap.org/ws/2004/09/transfer/Get"
ACTION_PUT = "http://schemas.xmlsoap.org/ws/2004/09/transfer/Put"

# CIM RequestPowerStateChange values
POWER_ON = 2
POWER_OFF_SOFT = 8
POWER_OFF_HARD = 6
POWER_RESET = 10

# SetBootConfigRole roles
ROLE_NEXT_SINGLE = 1

Notify = Callable[[str], None]

# dnsmasq names a client's TFTP directory this way: lowercase, dashes
MAC_RE = re.compile(r"^([0-9a-f]{2}-){5}[0-9a-f]{2}$")


def _stderr_notify(msg: str) -> None:
    print(msg, file=sys.stderr)


def localname(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def _esc(s: str) -> str:
    return (str(s).replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;").replace('"', "&quot;"))


class AmtError(RuntimeError):
    pass


# Minimal WS-MAN client for AMT power + PXE boot (HTTP digest).
class Amt:
    def __init__(self, host: str, password: str, user: str = "admin",
                 tls: bool = False, timeout: float = 30.0):
        self.host = host
        scheme, port = ("https", 16993) if tls else ("http", 16992)
        self.url = f"{scheme}://{host}:{port}/wsman"
        self.tls = tls
        self.timeout = timeout

        pwmgr = urllib.request.HTTPPasswordMgrWithDefaultRealm()
        pwmgr.add_password(None, self.url, user, password)

        handlers: List[urllib.request.BaseHandler] = [
            urllib.request.ProxyHandler({}),
            urllib.request.HTTPDigestAuthHandler(pwmgr),
        ]
        if tls:
            # AMT certificates are self-signed
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            handlers.append(urllib.request.HTTPSHandler(context=ctx))
        self.opener = urllib.request.build_opener(*handlers)

    def _post(self, body: str) -> ET.Element:
        req = urllib.request.Request(
            self.url, data=body.encode("utf-8"),
            headers={
                "Content-Type": "application/soap+xml;charset=UTF-8",
            },
        )
        try:
            with self.opener.open(req, timeout=self.timeout) as resp:
                text = resp.read().decode("utf-8", "replace")
        except urllib.error.HTTPError as e:
            if e.code == 401:
                raise AmtError(
                    "401 Unauthorized, check the user and password "
                    "and that the digest admin account is enabled in "
                    "MEBx"
                )
            detail = e.read().decode("utf-8", "replace")[:1000]
            raise AmtError(f"HTTP {e.code} from AMT:\n{detail}")
        except urllib.error.URLError as e:
            raise AmtError(f"AMT unreachable at {self.url}: {e.reason}")
        except OSError as e:
            raise AmtError(f"AMT unreachable at {self.url}: {e}")

        root = ET.fromstring(text)
        fault = root.find(".//{*}Fault")
        if fault is not None:
            reason = "".join(
                t.text or "" for t in fault.iter()
                if localname(t.tag) == "Text"
            )
            raise AmtError(
                f"WS-MAN fault: {reason.strip() or text[:500]}"
            )
        return root

    def _header(self, uri: str, action: str,
                selectors: Optional[Dict[str, str]] = None) -> str:
        sel = ""
        if selectors:
            items = "".join(
                f'<w:Selector Name="{k}">{_esc(v)}</w:Selector>'
                for k, v in selectors.items()
            )
            sel = f"<w:SelectorSet>{items}</w:SelectorSet>"
        return f"""
    <a:To>{self.url}</a:To>
    <w:ResourceURI>{uri}</w:ResourceURI>
    <a:ReplyTo><a:Address>{ANON}</a:Address></a:ReplyTo>
    <a:Action s:mustUnderstand="true">{action}</a:Action>
    <w:MaxEnvelopeSize s:mustUnderstand="true">512000</w:MaxEnvelopeSize>
    <a:MessageID>uuid:{uuid.uuid4()}</a:MessageID>
    <w:OperationTimeout>PT60S</w:OperationTimeout>
    {sel}"""

    def _envelope(self, header: str, body: str) -> str:
        return (
            '<?xml version="1.0" encoding="UTF-8"?>'
            '<s:Envelope xmlns:s="%(s)s" xmlns:a="%(a)s" '
            'xmlns:w="%(w)s">'
            '<s:Header>%(h)s</s:Header>'
            '<s:Body>%(b)s</s:Body></s:Envelope>'
        ) % {"s": NS["s"], "a": NS["a"], "w": NS["w"],
             "h": header, "b": body}

    def _epr(self, tag: str, uri: str,
             selectors: Dict[str, str]) -> str:
        items = "".join(
            f'<w:Selector Name="{k}">{_esc(v)}</w:Selector>'
            for k, v in selectors.items()
        )
        return (
            f"<r:{tag}><a:Address>{ANON}</a:Address>"
            "<a:ReferenceParameters>"
            f"<w:ResourceURI>{uri}</w:ResourceURI>"
            f"<w:SelectorSet>{items}</w:SelectorSet>"
            "</a:ReferenceParameters>"
            f"</r:{tag}>"
        )

    # The MAC of the wired port, the one a PXE boot uses.
    def mac_address(self) -> str:
        sel = {"InstanceID": INST_ETHERNET_WIRED}
        root = self._post(self._envelope(
            self._header(URI_ETHERNET_PORT, ACTION_GET, sel), ""
        ))
        for el in root.iter():
            if localname(el.tag) == "MACAddress" and el.text:
                return el.text.strip().lower()
        raise AmtError("MACAddress not found in response")

    def power_state(self) -> int:
        env = self._envelope(
            self._header(URI_ASSOC_POWER, ACTION_GET), ""
        )
        root = self._post(env)
        for el in root.iter():
            if localname(el.tag) == "PowerState":
                return int(el.text or "0")
        raise AmtError("PowerState not found in response")

    def is_on(self) -> bool:
        return self.power_state() == POWER_ON

    def set_power(self, state: int) -> None:
        action = f"{URI_POWER_SERVICE}/RequestPowerStateChange"
        header = self._header(
            URI_POWER_SERVICE, action, {"Name": NAME_POWER_SERVICE}
        )
        body = (
            '<r:RequestPowerStateChange_INPUT '
            f'xmlns:r="{URI_POWER_SERVICE}">'
            f"<r:PowerState>{state}</r:PowerState>"
            "<r:ManagedElement>"
            f"<a:Address>{ANON}</a:Address>"
            "<a:ReferenceParameters>"
            f"<w:ResourceURI>{URI_COMPUTER}</w:ResourceURI>"
            "<w:SelectorSet>"
            '<w:Selector Name="Name">ManagedSystem</w:Selector>'
            "</w:SelectorSet>"
            "</a:ReferenceParameters></r:ManagedElement>"
            "</r:RequestPowerStateChange_INPUT>"
        )
        root = self._post(self._envelope(header, body))
        rv = next(
            (e.text for e in root.iter()
             if localname(e.tag) == "ReturnValue"),
            None,
        )
        if rv not in (None, "0"):
            hint = ""
            if rv == "2":
                hint = (
                    " (commonly: the machine is already in that state, "
                    "or the transition is invalid from the current "
                    "power state)"
                )
            raise AmtError(
                f"RequestPowerStateChange returned {rv}{hint}"
            )

    # Poll the power state for up to settle seconds, true once
    # the machine reports anything but on.
    def wait_off(self, settle: float, poll: float = 1.0) -> bool:
        deadline = time.monotonic() + settle
        while True:
            try:
                if not self.is_on():
                    return True
            except AmtError:
                pass
            if time.monotonic() >= deadline:
                return False
            time.sleep(poll)

    def power_off(self, hard: bool = False, attempts: int = 6,
                  delay: float = 2.5, settle: float = 10.0,
                  notify: Notify = _stderr_notify) -> str:
        order = (
            [POWER_OFF_HARD] if hard
            else [POWER_OFF_SOFT, POWER_OFF_HARD]
        )
        last: Optional[AmtError] = None
        for i in range(attempts):
            for st in list(order):
                try:
                    self.set_power(st)
                except AmtError as e:
                    last = e
                    if st == POWER_OFF_SOFT:
                        notify("soft-off refused, forcing hard-off")
                        order.remove(POWER_OFF_SOFT)
                    continue
                if st == POWER_OFF_HARD:
                    return "hard"
                if self.wait_off(settle):
                    return "soft"
                notify(f"still on {settle:g}s after soft-off, "
                       "forcing hard-off")
                order.remove(POWER_OFF_SOFT)
            if i < attempts - 1:
                time.sleep(delay)
        assert last is not None
        raise last

    # GET AMT_BootSettingData, clear stale flags, set UseSOL, PUT.
    #
    # IDE-R stays off (PXE media comes from the network). UseSOL,
    # when on, redirects the pre-OS BIOS/PXE console to SOL so that
    # phase is captured too.
    def _put_boot_settings(self, use_sol: bool) -> None:
        sel = {"InstanceID": INST_BOOT_SETTING}
        root = self._post(self._envelope(
            self._header(URI_BOOT_SETTING, ACTION_GET, sel), ""
        ))
        bsd = next(
            (e for e in root.iter()
             if localname(e.tag) == "AMT_BootSettingData"),
            None,
        )
        if bsd is None:
            raise AmtError("AMT_BootSettingData not returned by GET")

        wanted = {
            "UseIDER": "false",
            "UseSOL": "true" if use_sol else "false",
            "UseSafeMode": "false",
            "ReflashBIOS": "false",
            "BIOSSetup": "false",
            "BIOSPause": "false",
            "LockPowerButton": "false",
            "LockResetButton": "false",
            "LockKeyboard": "false",
            "LockSleepButton": "false",
            "UserPasswordBypass": "false",
            "ForcedProgressEvents": "false",
        }
        for child in list(bsd):
            name = localname(child.tag)
            if name in wanted:
                child.text = wanted[name]

        ns_uri = bsd.tag[1:].split("}", 1)[0]
        ET.register_namespace("g", ns_uri)
        body = ET.tostring(bsd, encoding="unicode")
        self._post(self._envelope(
            self._header(URI_BOOT_SETTING, ACTION_PUT, sel), body
        ))

    def _set_pxe_boot_source(self) -> None:
        # ChangeBootOrder with a single Source replaces any stale
        # source left by a previous run (e.g. a forced CD/HDD boot)
        action = f"{URI_BOOT_CONFIG}/ChangeBootOrder"
        header = self._header(
            URI_BOOT_CONFIG, action, {"InstanceID": INST_BOOT_CONFIG}
        )
        body = (
            f'<r:ChangeBootOrder_INPUT xmlns:r="{URI_BOOT_CONFIG}">'
            + self._epr("Source", URI_BOOT_SOURCE,
                        {"InstanceID": INST_BOOT_PXE})
            + "</r:ChangeBootOrder_INPUT>"
        )
        self._post(self._envelope(header, body))

    def _set_boot_role_next(self) -> None:
        action = f"{URI_BOOT_SERVICE}/SetBootConfigRole"
        header = self._header(
            URI_BOOT_SERVICE, action, {"Name": NAME_BOOT_SERVICE}
        )
        body = (
            f'<r:SetBootConfigRole_INPUT xmlns:r="{URI_BOOT_SERVICE}">'
            + self._epr("BootConfigSetting", URI_BOOT_CONFIG,
                        {"InstanceID": INST_BOOT_CONFIG})
            + f"<r:Role>{ROLE_NEXT_SINGLE}</r:Role>"
            + "</r:SetBootConfigRole_INPUT>"
        )
        self._post(self._envelope(header, body))

    # Configure the next boot as PXE, then power on or reset.
    #
    # AMT applies the boot options asynchronously, so a power
    # change right after the write races the commit and the
    # machine starts POST with the previous options in effect,
    # neither PXE booting nor redirecting serial. commit_delay
    # lets the options settle first.
    def boot_into_pxe(self, use_sol: bool = True,
                      skip_boot_source: bool = False,
                      commit_delay: float = 3.0) -> None:
        on = self.is_on()
        self._put_boot_settings(use_sol=use_sol)
        if not skip_boot_source:
            self._set_pxe_boot_source()
        self._set_boot_role_next()
        if commit_delay:
            time.sleep(commit_delay)
        self.set_power(POWER_RESET if on else POWER_ON)


# Trigger a one-time PXE boot via meshcmd's own power command
# (amtpower --bootdevice pxe), the exact path MeshCommander uses.
# Reset if the machine is already on, else power on.
def boot_pxe_via_meshcmd(meshcmd: str, host: str, user: str,
                         password: str, tls: bool, on: bool) -> str:
    action = "--reset" if on else "--poweron"
    cmd = [meshcmd, "amtpower", action, "--bootdevice", "pxe",
           "--host", host, "--user", user, "--pass", password]
    if tls:
        cmd.append("--tls")
    r = subprocess.run(cmd, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT)
    out = r.stdout.decode("utf-8", "replace").strip()
    if r.returncode != 0:
        raise AmtError(
            f"meshcmd amtpower failed (rc={r.returncode}): {out}"
        )
    return out


class SolSession:
    def __init__(self, cmd: List[str], logfile: str,
                 env: Optional[Dict[str, str]] = None):
        self.logfile = logfile
        self._recording = True
        master, slave = pty.openpty()
        self._master = master
        self._file = open(logfile, "wb")
        try:
            self.proc = subprocess.Popen(
                cmd, stdin=slave, stdout=slave, stderr=slave,
                env=env, close_fds=True,
            )
        except OSError:
            os.close(master)
            os.close(slave)
            self._file.close()
            raise
        os.close(slave)
        self._reader = threading.Thread(target=self._pump, daemon=True)
        self._reader.start()

    def _pump(self) -> None:
        while True:
            try:
                data = os.read(self._master, 65536)
            except OSError:
                break          # EIO once the child closes the slave
            if not data:
                break
            if not self._recording:
                continue
            self._file.write(data)
            self._file.flush()

    def poll(self) -> Optional[int]:
        return self.proc.poll()

    @property
    def returncode(self) -> Optional[int]:
        return self.proc.returncode

    def stop(self) -> None:
        # Let the pump drain, then stop recording before terminating,
        # the SOL clients print teardown noise (exception traces,
        # Disconnected) on the way out
        time.sleep(0.2)
        self._recording = False
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        try:
            os.close(self._master)
        except OSError:
            pass
        self._reader.join(timeout=2)
        try:
            self._file.flush()
            self._file.close()
        except OSError:
            pass


# Strip VT/ANSI control sequences (CSI, OSC, two-char escapes) so the
# OS's colour/clear-screen codes don't drive the reader's terminal.
_ANSI_RE = re.compile(
    rb"\x1b(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~]|\][^\x07]*\x07)"
)
_CTRL_RE = re.compile(rb"[\x00-\x08\x0b\x0c\x0e-\x1f]")


def clean_serial(buf: bytes) -> bytes:
    # Reduce raw SOL bytes to plain text. Only a line feed starts a new line
    buf = _ANSI_RE.sub(b"", buf)
    buf = buf.replace(b"\r", b"")
    return _CTRL_RE.sub(b"", buf)


class MachineConfig:
    def __init__(self, raw: Dict[str, object]):
        def get_str(key: str, default: str) -> str:
            v = raw.get(key, default)
            if not isinstance(v, str):
                raise ValueError(f"machine {key!r} must be a string")
            return v

        def get_float(key: str, default: float) -> float:
            v = raw.get(key, default)
            if not isinstance(v, (int, float)):
                raise ValueError(f"machine {key!r} must be a number")
            return float(v)

        self.name = get_str("name", "")
        if not self.name:
            raise ValueError("machine entry without a name")
        self.description = get_str("description", "")
        self.driver = get_str("driver", "amt")
        if self.driver not in ("amt", "mock"):
            raise ValueError(
                f"machine {self.name}: bad driver {self.driver!r}"
            )
        self.host = get_str("host", "")
        if self.driver == "amt" and not self.host:
            raise ValueError(f"machine {self.name}: no host")
        self.user = get_str("user", "admin")
        self.password = get_str("password", "")
        self.tls = bool(raw.get("tls", False))
        self.boot_method = get_str("boot_method", "wsman")
        self.sol_tool = get_str("sol_tool", "meshcmd")
        self.skip_boot_source = bool(raw.get("skip_boot_source", False))
        self.commit_delay = get_float("commit_delay", 3.0)
        self.meshcmd = get_str("meshcmd", "meshcmd")
        self.amtterm = get_str("amtterm", "amtterm")
        self.mac = get_str("mac", "").lower().replace(":", "-")
        if self.mac and not MAC_RE.match(self.mac):
            raise ValueError(f"machine {self.name}: bad mac {self.mac!r}")
        self.console_cmdline = get_str("console_cmdline", "")
        if self.console_cmdline and not self.mac:
            raise ValueError(
                f"machine {self.name}: MAC is required to serve "
                "machine-specific command line"
            )

        if self.boot_method not in ("wsman", "meshcmd"):
            raise ValueError(
                f"machine {self.name}: bad boot_method "
                f"{self.boot_method!r}"
            )
        if self.sol_tool not in ("meshcmd", "amtterm"):
            raise ValueError(
                f"machine {self.name}: bad sol_tool {self.sol_tool!r}"
            )

        # mock driver knobs (testing without hardware)
        lines = raw.get("mock_lines", [])
        if not isinstance(lines, list):
            raise ValueError(f"machine {self.name}: bad mock_lines")
        self.mock_lines = [str(x) for x in lines]
        self.mock_boot_delay = get_float("mock_boot_delay", 0.5)
        self.mock_line_delay = get_float("mock_line_delay", 0.1)

    def describe(self) -> Dict[str, object]:
        return {
            "name": self.name,
            "description": self.description,
            "driver": self.driver,
            "host": self.host,
            "boot_method": self.boot_method,
            "sol_tool": self.sol_tool,
            "mac": self.mac,
            "console_cmdline": self.console_cmdline,
        }


class Machine:
    def __init__(self, config: MachineConfig,
                 notify: Notify = _stderr_notify):
        self.config = config
        self.notify = notify

    def power_state(self) -> int:
        raise NotImplementedError

    def mac_address(self) -> str:
        raise NotImplementedError

    def is_on(self) -> bool:
        return self.power_state() == POWER_ON

    # Force a one-time PXE boot (reset if on, power on if off).
    def boot(self) -> None:
        raise NotImplementedError

    def power_off(self) -> None:
        raise NotImplementedError

    # Start capturing serial into logfile. SOL runs on the ME,
    # so it connects while the box is still off and streams once
    # it powers on, started before boot() nothing is missed.
    def start_sol(self, logfile: str) -> SolSession:
        raise NotImplementedError


class AmtMachine(Machine):
    def __init__(self, config: MachineConfig,
                 notify: Notify = _stderr_notify):
        super().__init__(config, notify)
        self.amt = Amt(config.host, config.password, user=config.user,
                       tls=config.tls)

    def power_state(self) -> int:
        return self.amt.power_state()

    def mac_address(self) -> str:
        return self.amt.mac_address()

    def boot(self) -> None:
        cfg = self.config
        if cfg.boot_method == "meshcmd":
            meshcmd = shutil.which(cfg.meshcmd) or cfg.meshcmd
            boot_pxe_via_meshcmd(meshcmd, cfg.host, cfg.user,
                                 cfg.password, cfg.tls,
                                 self.amt.is_on())
        else:
            self.amt.boot_into_pxe(
                use_sol=True,
                skip_boot_source=cfg.skip_boot_source,
                commit_delay=cfg.commit_delay,
            )

    def power_off(self) -> None:
        self.amt.power_off(notify=self.notify)

    def start_sol(self, logfile: str) -> SolSession:
        cfg = self.config
        env: Optional[Dict[str, str]] = None
        if cfg.sol_tool == "meshcmd":
            tool = shutil.which(cfg.meshcmd) or cfg.meshcmd
            cmd = [tool, "amtterm", "--host", cfg.host,
                   "--user", cfg.user, "--pass", cfg.password]
            if cfg.tls:
                cmd.append("--tls")
        else:
            tool = shutil.which(cfg.amtterm) or cfg.amtterm
            cmd = [tool, "-u", cfg.user]
            if cfg.tls:
                cmd.append("-z")
            cmd.append(cfg.host)
            env = dict(os.environ, AMT_PASSWORD=cfg.password)
        return SolSession(cmd, logfile, env=env)


# Fake machine for testing the server without hardware: 'boot'
# starts a thread that types the configured lines into the log
# file at a configured pace.
class MockMachine(Machine):
    def __init__(self, config: MachineConfig,
                 notify: Notify = _stderr_notify):
        super().__init__(config, notify)
        self._on = False
        self._writer: Optional[threading.Thread] = None
        self._stop = threading.Event()
        self._logfile = ""

    def power_state(self) -> int:
        return POWER_ON if self._on else POWER_OFF_HARD

    def mac_address(self) -> str:
        raise AmtError("a mock machine has no MAC")

    def boot(self) -> None:
        self._stop.set()
        if self._writer is not None:
            self._writer.join(timeout=5)
        self._stop = threading.Event()
        self._on = True
        self._writer = threading.Thread(target=self._type, daemon=True)
        self._writer.start()

    def _type(self) -> None:
        stop = self._stop
        if stop.wait(self.config.mock_boot_delay):
            return
        with open(self._logfile, "ab") as f:
            for line in self.config.mock_lines:
                f.write(line.encode() + b"\r\n")
                f.flush()
                if stop.wait(self.config.mock_line_delay):
                    return

    def power_off(self) -> None:
        self._on = False
        self._stop.set()

    def start_sol(self, logfile: str) -> SolSession:
        self._logfile = logfile
        # A real idle child gives poll() and stop() the semantics of
        # the AMT tools
        return SolSession(
            [sys.executable, "-c", "import time\ntime.sleep(3600)"],
            logfile,
        )


def machine_from_config(config: MachineConfig,
                        notify: Notify = _stderr_notify) -> Machine:
    if config.driver == "amt":
        return AmtMachine(config, notify)
    if config.driver == "mock":
        return MockMachine(config, notify)
    raise ValueError(f"unknown machine driver {config.driver!r}")
