#!/usr/bin/env python3
"""
Send this computer's stats to the ESP32 CYD "PC" page.

    pip install psutil
    python pc_monitor.py                      # board at http://clawd.local
    python pc_monitor.py --host 192.168.1.23  # or use the IP shown on the board

Also sends the MAC of the network card used to reach the board, so the board's
Wake-on-LAN page can turn this PC on later.

What gets sent (every --interval seconds, POST http://<host>/pc):
    CPU load %, RAM                               -> psutil (all OS)
    CPU temperature                               -> LibreHardwareMonitor (Windows)
                                                     or psutil sensors (Linux)
    GPU load %, temperature, VRAM used/total      -> LibreHardwareMonitor, or nvidia-smi
    CPU / GPU model name ("Ryzen 7 5700X", "RTX 3060") -> registry / nvidia-smi
    CPU clock + package power                     -> LibreHardwareMonitor
    GPU core clock + power                        -> LibreHardwareMonitor, or nvidia-smi

CPU temperature on Windows needs LibreHardwareMonitor running (as admin) with
Options -> Remote Web Server -> Run (default port 8085). Without it the board
shows "--" for the temperatures; everything else still works.

Also fetches the USD/VND rate from Vietcombank's JSON API (every 30 min) and
sends it along (usd_buy / usd_sell / usd_t) for the board's prices page:
Vietcombank's bot filter often stalls the ESP32's own requests. --no-usd to skip.

Run it hidden on Windows with run_pc_monitor.bat (uses pythonw, no console).
"""
import argparse
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

try:
    import psutil
except ImportError:
    sys.exit("Missing psutil -> run: pip install psutil")

IS_WIN = os.name == "nt"
NO_WINDOW = 0x08000000 if IS_WIN else 0  # CREATE_NO_WINDOW: no console flash under pythonw

# Never route LAN/localhost requests through a system proxy (urllib reads the
# Windows proxy settings by default, which breaks talking to the board).
HTTP = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


# ---------------------------------------------------------------- LibreHardwareMonitor

_NUM = re.compile(r"[-+]?\d+(?:[.,]\d+)?")


def _num(text):
    m = _NUM.search(text or "")
    return float(m.group(0).replace(",", ".")) if m else None


def _lhm_sensors(node, hw="", group="", out=None):
    """Flatten LHM's data.json tree -> list of (hardware, group, name, sensor_id, value, raw)."""
    if out is None:
        out = []
    text = node.get("Text", "")
    children = node.get("Children") or []
    sid = node.get("SensorId") or ""
    if not children and node.get("Value") not in (None, ""):
        out.append((hw, group, text, sid, _num(node.get("Value")), node.get("Value")))
        return out
    # Hardware nodes carry an icon (cpu.png, nvidia.png, ...); sensor groups don't.
    img = (node.get("ImageURL") or "").lower()
    if text in ("Temperatures", "Load", "Clocks", "Powers", "Fans", "Data", "Voltages",
                "Controls", "SmallData", "Throughput", "Factors", "Levels", "Currents"):
        group = text
    elif img and "transparent" not in img:
        hw = text
    for c in children:
        _lhm_sensors(c, hw, group, out)
    return out


def _is_cpu(hw, sid):
    return sid.startswith(("/intelcpu", "/amdcpu", "/cpu")) or bool(
        re.search(r"\b(intel core|core\(tm\)|ryzen|xeon|threadripper|athlon|pentium|celeron)\b", hw, re.I))


def _is_gpu(hw, sid):
    return sid.startswith("/gpu") or bool(
        re.search(r"\b(nvidia|geforce|rtx|gtx|quadro|radeon|arc)\b", hw, re.I))


def _kind(group, sid):
    if "/temperature/" in sid or group == "Temperatures":
        return "temp"
    if "/load/" in sid or group == "Load":
        return "load"
    if "/smalldata/" in sid or "/data/" in sid or group in ("SmallData", "Data"):
        return "mem"
    if "/power/" in sid or group == "Powers":
        return "power"
    if "/clock/" in sid or group == "Clocks":
        return "clock"
    return ""


def _to_gb(val, raw, group, sid):
    """LHM memory values: SmallData is MB, Data is GB - trust the unit in the text when present."""
    unit = (raw or "").upper()
    if "GB" in unit:
        return val
    if "KB" in unit:
        return val / 2**20
    if "MB" in unit or group == "SmallData" or "/smalldata/" in sid:
        return val / 1024
    return val


def _pick(items, preferred):
    """items: list of (name, value). First preferred name wins, else the max value."""
    vals = {n: v for n, v in items if v is not None}
    for p in preferred:
        for n, v in vals.items():
            if n.lower() == p.lower():
                return v
    return max(vals.values()) if vals else None


def read_lhm(url):
    try:
        with HTTP.open(url, timeout=0.8) as r:
            data = json.load(r)
    except (OSError, ValueError):
        return {}
    cpu_t, gpu_t, gpu_l, gpu_m = [], [], [], {}
    cpu_p, gpu_p, cpu_c, gpu_c = [], [], [], []
    out = {}
    for hw, group, name, sid, val, raw in _lhm_sensors(data):
        k = _kind(group, sid)
        if _is_cpu(hw, sid):
            if hw:
                out.setdefault("cpu_hw", hw)
            if k == "power":
                cpu_p.append((name, val))
            elif k == "clock" and val and re.fullmatch(r"(Core #\d+|Cores \(Average\))", name):
                cpu_c.append((name, val))
        elif _is_gpu(hw, sid):
            if hw:
                out.setdefault("gpu_hw", hw)
            if k == "power":
                gpu_p.append((name, val))
            elif k == "clock":
                gpu_c.append((name, val))
        if _is_gpu(hw, sid) and k == "mem" and val is not None:
            gpu_m.setdefault(name.lower(), _to_gb(val, raw, group, sid))
        if _is_cpu(hw, sid) and k == "temp":
            cpu_t.append((name, val))
        elif _is_gpu(hw, sid) and k == "temp":
            gpu_t.append((name, val))
        elif _is_gpu(hw, sid) and k == "load":
            gpu_l.append((name, val))
    v = _pick(cpu_t, ["CPU Package", "Core (Tctl/Tdie)", "Core (Tctl)", "Tctl/Tdie",
                      "CPU (Tctl/Tdie)", "Core Average", "Core Max"])
    if v is not None:
        out["cpu_temp"] = v
    v = _pick(gpu_t, ["GPU Core", "GPU Temperature"])
    if v is not None:
        out["gpu_temp"] = v
    gl = {n: v for n, v in gpu_l if v is not None}
    if "GPU Core" in gl:
        out["gpu"] = gl["GPU Core"]
    elif gl:
        out["gpu"] = max(gl.values())

    # VRAM (GB): dedicated GPU memory used / total (or used + free)
    def first(*names):
        return next((gpu_m[n] for n in names if n in gpu_m), None)
    used = first("gpu memory used", "d3d dedicated memory used")
    total = first("gpu memory total", "d3d dedicated memory total")
    free = first("gpu memory free", "d3d dedicated memory free")
    if total is None and used is not None and free is not None:
        total = used + free
    if used is not None and total:
        out["vram_used"], out["vram_total"] = used, total

    # Power (W) and clocks (MHz)
    v = _pick(cpu_p, ["Package", "CPU Package"])
    if v is not None:
        out["cpu_power"] = v
    v = _pick(gpu_p, ["GPU Package", "GPU Power", "GPU Board Power"])
    if v is not None:
        out["gpu_power"] = v
    avg = [v for n, v in cpu_c if n == "Cores (Average)"]
    cores = [v for n, v in cpu_c if n.startswith("Core #")]
    if avg:
        out["cpu_clock"] = avg[0]
    elif cores:
        out["cpu_clock"] = sum(cores) / len(cores)
    v = dict((n, v) for n, v in gpu_c if v is not None).get("GPU Core")
    if v:
        out["gpu_clock"] = v
    return out


# ---------------------------------------------------------------- nvidia-smi

NVSMI = shutil.which("nvidia-smi")


def read_nvidia():
    if not NVSMI:
        return {}
    try:
        r = subprocess.run(
            [NVSMI, "--query-gpu=utilization.gpu,temperature.gpu,memory.used,memory.total,"
                    "clocks.gr,power.draw",
             "--format=csv,noheader,nounits"],
            capture_output=True, text=True, timeout=2, creationflags=NO_WINDOW)
        f = [_num(x) for x in r.stdout.splitlines()[0].split(",")]  # "[N/A]" -> None
    except Exception:
        return {}
    out = {}
    for key, i, scale in (("gpu", 0, 1), ("gpu_temp", 1, 1), ("vram_used", 2, 1 / 1024),
                          ("vram_total", 3, 1 / 1024), ("gpu_clock", 4, 1), ("gpu_power", 5, 1)):
        if i < len(f) and f[i] is not None:
            out[key] = f[i] * scale  # VRAM: MiB -> GB
    return out


# ---------------------------------------------------------------- model names

def short_name(name):
    """'AMD Ryzen 7 5700X 8-Core Processor' -> 'Ryzen 7 5700X', 'NVIDIA GeForce RTX 3060' -> 'RTX 3060'."""
    n = re.sub(r"\((?:R|TM)\)", "", name or "", flags=re.I)
    n = re.sub(r"\s+@.*$", "", n)  # "@ 3.60GHz"
    n = re.sub(r"\b\d+-Core Processor\b|\bwith Radeon Graphics\b|\bProcessor\b|\bCPU\b", "", n, flags=re.I)
    n = re.sub(r"^\s*\d+(?:st|nd|rd|th) Gen\s+", "", n, flags=re.I)
    n = re.sub(r"^\s*(AMD|Intel|NVIDIA)\s+", "", n, flags=re.I)
    n = re.sub(r"^\s*(GeForce|Radeon|Core)\s+", "", n, flags=re.I)
    return " ".join(n.split())[:23]


def cpu_model():
    try:
        if IS_WIN:
            import winreg
            k = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\CentralProcessor\0")
            return winreg.QueryValueEx(k, "ProcessorNameString")[0]
        with open("/proc/cpuinfo") as f:
            for line in f:
                if line.startswith("model name"):
                    return line.split(":", 1)[1]
    except OSError:
        pass
    return ""


def gpu_model():
    if not NVSMI:
        return ""
    try:
        r = subprocess.run([NVSMI, "--query-gpu=name", "--format=csv,noheader"],
                           capture_output=True, text=True, timeout=3, creationflags=NO_WINDOW)
        return r.stdout.splitlines()[0].strip()
    except Exception:
        return ""


# ---------------------------------------------------------------- psutil

def read_linux_cpu_temp():
    try:
        temps = psutil.sensors_temperatures()
    except (AttributeError, OSError):
        return None
    for chip, label in (("coretemp", "Package id 0"), ("k10temp", "Tctl"),
                        ("k10temp", "Tdie"), ("zenpower", "Tdie"), ("cpu_thermal", "")):
        for e in temps.get(chip, []):
            if not label or e.label == label:
                return e.current
    for entries in temps.values():
        if entries:
            return entries[0].current
    return None


class Sampler:
    def __init__(self, lhm_url):
        self.lhm_url = lhm_url
        self.host = socket.gethostname()[:31]
        self.cpu_name = short_name(cpu_model())
        self.gpu_name = short_name(gpu_model())
        psutil.cpu_percent(None)  # prime: the first call always returns 0

    def sample(self):
        s = {"host": self.host, "cpu": psutil.cpu_percent(None)}

        vm = psutil.virtual_memory()
        s["ram_used"] = (vm.total - vm.available) / 2**30
        s["ram_total"] = vm.total / 2**30

        if not IS_WIN:
            t = read_linux_cpu_temp()
            if t is not None:
                s["cpu_temp"] = t

        hw = read_lhm(self.lhm_url) if self.lhm_url else {}
        if any(k not in hw for k in ("gpu", "gpu_temp", "vram_used", "gpu_clock", "gpu_power")):
            for k, v in read_nvidia().items():
                hw.setdefault(k, v)
        # model names: registry / nvidia-smi, else the hardware name LHM shows
        cpu_hw, gpu_hw = hw.pop("cpu_hw", ""), hw.pop("gpu_hw", "")
        if not self.cpu_name and cpu_hw:
            self.cpu_name = short_name(cpu_hw)
        if not self.gpu_name and gpu_hw:
            self.gpu_name = short_name(gpu_hw)
        if self.cpu_name:
            s["cpu_name"] = self.cpu_name
        if self.gpu_name:
            s["gpu_name"] = self.gpu_name
        s.update(hw)
        return s


# ---------------------------------------------------------------- Wake-on-LAN info

_WIFI_NAME = re.compile(r"wi-?fi|wlan|wireless|802\.11", re.I)


def nic_for(board_ip):
    """MAC + Wi-Fi flag of the network card this PC uses to reach the board.

    The board saves the MAC so it can wake this PC later (Wake-on-LAN page).
    """
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect((board_ip, 9))  # UDP connect sends nothing, just picks the route
        local_ip = s.getsockname()[0]
        s.close()
    except OSError:
        return None
    for name, addrs in psutil.net_if_addrs().items():
        if any(a.family == socket.AF_INET and a.address == local_ip for a in addrs):
            for a in addrs:
                if a.family == psutil.AF_LINK and a.address:
                    mac = a.address.replace("-", ":").upper()
                    if mac.strip("0:"):
                        return {"mac": mac, "nic": "wifi" if _WIFI_NAME.search(name) else "eth",
                                "name": name}
    return None


# ---------------------------------------------------------------- USD/VND (Vietcombank)

VCB_URL = "https://www.vietcombank.com.vn/api/exchangerates?date={date}"
VCB_UAS = ("curl/8.5.0",
           "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
           "Chrome/129.0 Safari/537.36")


def _vnd(v):
    """'25,600.00' / '25600.00' / 25600 -> 25600.0"""
    try:
        return float(str(v).replace(",", ""))
    except (TypeError, ValueError):
        return None


class UsdRate:
    """Fetches Vietcombank's USD buy (transfer) / sell in a background thread:
    every 30 min, every 2 min while failing. .fields() -> what to send."""

    def __init__(self):
        self._data = None
        self._lock = threading.Lock()
        threading.Thread(target=self._run, daemon=True).start()

    def fields(self):
        with self._lock:
            return dict(self._data) if self._data else {}

    def _run(self):
        while True:
            time.sleep(30 * 60 if self._fetch() else 120)

    def _fetch(self):
        url = VCB_URL.format(date=time.strftime("%Y-%m-%d"))  # PC's local date (Vietnam)
        err = None
        for ua in VCB_UAS:
            # system proxy is fine here (internet), unlike the board on the LAN
            req = urllib.request.Request(url, headers={"User-Agent": ua, "Accept": "application/json"})
            try:
                with urllib.request.urlopen(req, timeout=20) as r:
                    data = json.load(r)
                for c in data.get("Data") or []:
                    if c.get("currencyCode") == "USD":
                        buy = _vnd(c.get("transfer")) or _vnd(c.get("cash"))
                        sell = _vnd(c.get("sell"))
                        if buy and sell and 1e4 < buy < 1e5 and 1e4 < sell < 1e5:
                            with self._lock:
                                changed = not self._data or (self._data["usd_buy"], self._data["usd_sell"]) != (buy, sell)
                                self._data = {"usd_buy": buy, "usd_sell": sell, "usd_t": int(time.time())}
                            if changed:
                                log(f"USD (Vietcombank): buy {buy:,.0f}  sell {sell:,.0f}")
                            return True
                err = "no USD in the reply"
            except Exception as e:
                err = f"{e.__class__.__name__}: {e}"
        log(f"USD: can't get Vietcombank rate ({err}), retry in 2 min")
        return False


# ---------------------------------------------------------------- send

class Board:
    def __init__(self, host):
        name, _, port = host.partition(":")  # "clawd.local" or "192.168.1.23:80"
        self.host = name
        self.port = f":{port}" if port else ""
        self.ip = None
        self.nic = None

    def _resolve(self):
        # Resolve once and cache: .local (mDNS) lookups on Windows can take ~1s each.
        self.ip = socket.gethostbyname(self.host)
        if self.ip != getattr(self, "_logged_ip", None):
            log(f"board {self.host} -> {self.ip}")
            self._logged_ip = self.ip
            self.nic = nic_for(self.ip)
            if self.nic:
                log(f"Wake-on-LAN: {self.nic['name']} {self.nic['mac']}"
                    + ("  (Wi-Fi: WoL usually only works over Ethernet)" if self.nic["nic"] == "wifi" else ""))

    def send(self, stats):
        if self.ip is None:
            self._resolve()
        if self.nic:
            stats = dict(stats, mac=self.nic["mac"], nic=self.nic["nic"])
        body = urllib.parse.urlencode(
            {k: (f"{v:.2f}" if isinstance(v, float) else v) for k, v in stats.items()}).encode()
        req = urllib.request.Request(f"http://{self.ip}{self.port}/pc", data=body, method="POST")
        try:
            with HTTP.open(req, timeout=2) as r:
                r.read()
        except Exception:
            self.ip = None  # board may have a new IP after a reboot -> resolve again
            raise


def main():
    ap = argparse.ArgumentParser(description="Send PC stats to the ESP32 CYD board")
    ap.add_argument("--host", default="clawd.local", help="board hostname or IP (default clawd.local)")
    ap.add_argument("--interval", type=float, default=1.0, help="seconds between updates (default 1)")
    ap.add_argument("--lhm", default="http://localhost:8085/data.json",
                    help="LibreHardwareMonitor web server URL ('' to disable)")
    ap.add_argument("--once", action="store_true", help="print one sample and exit (no sending)")
    ap.add_argument("--no-usd", action="store_true", help="don't fetch/send the Vietcombank USD rate")
    a = ap.parse_args()

    sampler = Sampler(a.lhm)
    if a.once:
        if not a.no_usd:
            u = UsdRate.__new__(UsdRate)
            u._data, u._lock = None, threading.Lock()
            u._fetch()
            print("USD:", u.fields() or "failed")
        time.sleep(0.5)
        print(json.dumps(sampler.sample(), indent=2))
        return
    usd = None if a.no_usd else UsdRate()

    board = Board(a.host)
    log(f"sending to {a.host} every {a.interval}s  (Ctrl+C to stop)")
    fails = 0
    while True:
        t0 = time.monotonic()
        try:
            stats = sampler.sample()
            if usd:
                stats.update(usd.fields())
            board.send(stats)
            if fails:
                log("connected again")
            fails = 0
        except Exception as e:
            fails += 1
            if fails in (1, 10) or fails % 60 == 0:  # don't spam the console
                log(f"can't reach board ({e.__class__.__name__}: {e})")
            if fails > 3:
                time.sleep(3)  # board offline -> back off a little
        time.sleep(max(0.0, a.interval - (time.monotonic() - t0)))


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
