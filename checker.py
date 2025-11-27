import os
import sys
import time
import ssl
import socket
import ctypes
import subprocess
import urllib.request
import urllib.error
import urllib.parse
import winreg
import json
import re

RESET = "\x1b[0m"
BLUE = "\x1b[34m"
RED = "\x1b[31m"
GREEN = "\x1b[32m"
ORANGE = "\x1b[38;5;208m"
CYAN = "\x1b[36m"
SECTION_DELAY = 2
LOG_LINES = []

BATTLEMETRICS_API = "BAE-API"

def strip_ansi(s):
    try:
        return re.sub(r"\x1b\[[0-9;]*m", "", s)
    except Exception:
        return s

def enable_vt_mode():
    if os.name == "nt":
        kernel32 = ctypes.windll.kernel32
        h = kernel32.GetStdHandle(-11)
        mode = ctypes.c_uint()
        if kernel32.GetConsoleMode(h, ctypes.byref(mode)):
            kernel32.SetConsoleMode(h, mode.value | 0x0004)

def cprint(level, message):
    color = BLUE if level == "Info" else GREEN if level == "Success" else RED if level == "Error" else ORANGE
    print(f"{color}{level}{RESET} - {message}")
    LOG_LINES.append(f"{level} - {strip_ansi(message)}")

def delay():
    time.sleep(SECTION_DELAY)

def clear_console():
    os.system("cls" if os.name == "nt" else "clear")

def set_title(title):
    if os.name == "nt":
        try:
            ctypes.windll.kernel32.SetConsoleTitleW(title)
        except Exception:
            os.system(f"title {title}")
    else:
        sys.stdout.write(f"\x1b]0;{title}\x07")

def set_fixed_console(cols=125, lines=40, lock=False):
    try:
        os.system(f"mode con: cols={cols} lines={lines}")
        if lock:
            h = ctypes.windll.kernel32.GetConsoleWindow()
            if h:
                user32 = ctypes.windll.user32
                GWL_STYLE = -16
                WS_MAXIMIZEBOX = 0x00010000
                WS_THICKFRAME = 0x00040000
                style = user32.GetWindowLongW(h, GWL_STYLE)
                new_style = style & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME
                user32.SetWindowLongW(h, GWL_STYLE, new_style)
                user32.SetWindowPos(h, 0, 0, 0, 0, 0, 0x0020 | 0x0001 | 0x0002 | 0x0004)
    except Exception:
        pass

def is_admin():
    try:
        return ctypes.windll.shell32.IsUserAnAdmin() != 0
    except:
        return False

def http_ping(url, timeout=5):
    ctx = ssl.create_default_context()
    start = time.perf_counter()
    with urllib.request.urlopen(url, timeout=timeout, context=ctx) as resp:
        resp.read(1)
    end = time.perf_counter()
    return int(round((end - start) * 1000))

def fmt_stats(samples):
    if not samples:
        return ""
    mn = min(samples)
    mx = max(samples)
    avg = int(round(sum(samples) / len(samples)))
    return f"(min: {mn} ms | average: {avg} ms | max: {mx} ms)"

def http_ping_multi(url, timeout=5, count=5):
    values = []
    for _ in range(count):
        try:
            values.append(http_ping(url, timeout=timeout))
        except Exception:
            pass
    return values

def tcp_ping(host, port, timeout=3):
    start = time.perf_counter()
    with socket.create_connection((host, port), timeout=timeout):
        pass
    end = time.perf_counter()
    return int(round((end - start) * 1000))

def check_network():
    cprint("Info", "Checking network connection")
    try:
        samples = http_ping_multi("https://www.google.com/generate_204", timeout=5, count=5)
        if samples:
            ms = int(round(sum(samples) / len(samples)))
            cprint("Success", f"Network is connected ({ms} ms)")
            return True, samples
        else:
            cprint("Error", "Network is not connected")
            return False, None
    except urllib.error.URLError:
        cprint("Error", "Network is not connected")
        return False, None
    except Exception:
        cprint("Error", "Could not check for network connection")
        return False, None

def detect_proxy():
    env = [os.environ.get("HTTP_PROXY"), os.environ.get("HTTPS_PROXY"), os.environ.get("http_proxy"), os.environ.get("https_proxy")]
    env = [e for e in env if e]
    reg_enabled = False
    reg_server = None
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Microsoft\Windows\CurrentVersion\Internet Settings") as k:
            reg_enabled = winreg.QueryValueEx(k, "ProxyEnable")[0] == 1
            try:
                reg_server = winreg.QueryValueEx(k, "ProxyServer")[0]
            except OSError:
                reg_server = None
    except OSError:
        pass
    configured = bool(env) or reg_enabled
    detail = env[0] if env else reg_server
    return configured, detail

def detect_vpn():
    try:
        out = subprocess.run(["ipconfig", "/all"], capture_output=True, text=True, timeout=7)
        text = out.stdout.lower()
        keywords = ["vpn", "tap", "tun", "wireguard", "anyconnect", "openvpn", "ikev2", "suricata", "expressvpn", "protonvpn", "nordvpn", "surfshark", "windscribe", "hideme"]
        detected = any(k in text for k in keywords)
        return detected
    except Exception:
        return False

def check_website():
    cprint("Info", "Checking Rusticaland Website Response")
    try:
        samples = http_ping_multi("https://rusticaland.net/", timeout=7, count=1)
        if samples:
            ms = int(round(sum(samples) / len(samples)))
            cprint("Success", f"Rusticaland Website ({ms} ms)")
            print("")
            return True, samples
        else:
            cprint("Error", "Could not connect to rusticaland website")
            return False, None
    except Exception:
        cprint("Error", "Could not connect to rusticaland website")
        return False, None

def a2s_query(host, port, timeout=2):
    payload = b"\xFF\xFF\xFF\xFFTSource Engine Query\x00"
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(timeout)
    start = time.perf_counter()
    s.sendto(payload, (host, port))
    data, _ = s.recvfrom(4096)
    if len(data) >= 5 and data[:4] == b"\xFF\xFF\xFF\xFF" and data[4] == 0x41:
        token = data[5:9]
        s.sendto(payload + token, (host, port))
        data, _ = s.recvfrom(4096)
    end = time.perf_counter()
    s.close()
    return int(round((end - start) * 1000))

def a2s_query_multi(host, port, count=5):
    values = []
    for _ in range(count):
        try:
            values.append(a2s_query(host, port, timeout=2))
            continue
        except Exception:
            pass
        try:
            values.append(a2s_query(host, port + 1, timeout=2))
        except Exception:
            pass
        try:
            values.append(a2s_query(host, port - 1, timeout=2))
        except Exception:
            pass
    return values

def check_server(host, port, label):
    cprint("Info", f"Checking {label} Response")
    try:
        samples = a2s_query_multi(host, port, count=5)
        if samples:
            stats = fmt_stats(samples)
            cprint("Success", f"{label} {CYAN}{stats}{RESET}")
            return True, samples
        else:
            return False, None
    except Exception:
        return False, None

def battlemetrics_status(host, port, label):
    try:
        q1 = urllib.parse.quote(label)
        url1 = f"https://api.battlemetrics.com/servers?filter[game]=rust&filter[search]={q1}"
        req = urllib.request.Request(url1)
        if BATTLEMETRICS_API:
            req.add_header("Authorization", f"Bearer {BATTLEMETRICS_API}")
        with urllib.request.urlopen(req, timeout=8) as resp:
            data = json.loads(resp.read().decode("utf-8", errors="ignore"))
        arr = data.get("data") or []
        if not arr:
            q2 = urllib.parse.quote(host)
            url2 = f"https://api.battlemetrics.com/servers?filter[game]=rust&filter[search]={q2}"
            req2 = urllib.request.Request(url2)
            if BATTLEMETRICS_API:
                req2.add_header("Authorization", f"Bearer {BATTLEMETRICS_API}")
            with urllib.request.urlopen(req2, timeout=8) as resp2:
                data2 = json.loads(resp2.read().decode("utf-8", errors="ignore"))
            arr = data2.get("data") or []
        pick = None
        for item in arr:
            a = item.get("attributes") or {}
            nm = a.get("name") or ""
            ip = a.get("ip") or a.get("host") or ""
            pr = int(a.get("port") or a.get("portQuery") or 0)
            if (host in ip) and (int(port) == pr):
                pick = a
                break
            if label.lower() in nm.lower():
                pick = a
        if not pick:
            cprint("Info", f"{label} not listed on BattleMetrics")
            return None
        status = (pick.get("status") or pick.get("state") or "unknown").lower()
        if status == "online":
            cprint("Success", f"{label} Online")
        elif status == "offline":
            cprint("Warning", f"{label} Offline")
        else:
            cprint("Info", f"{label} status {status}")
        return int(pick.get("portQuery") or 0) or None
    except Exception:
        cprint("Error", f"Could not check {label} status")
        return None

def check_server_flow(host, port, label):
    ok, samples = check_server(host, port, label)
    if ok:
        battlemetrics_status(host, port, label)
        return True
    bm_port = battlemetrics_status(host, port, label)
    if bm_port:
        try:
            samples2 = a2s_query_multi(host, bm_port, count=5)
            if samples2:
                stats2 = fmt_stats(samples2)
                cprint("Success", f"{label} {CYAN}{stats2}{RESET}")
                return True
        except Exception:
            pass
    cprint("Error", f"Could not connect to {label}")
    return False

def firewall_state():
    try:
        out = subprocess.run(["netsh", "advfirewall", "show", "currentprofile"], capture_output=True, text=True, timeout=5)
        t = out.stdout.lower()
        return "state" in t and "on" in t
    except Exception:
        return False

def firewall_rules_ports(ports):
    try:
        out = subprocess.run(["netsh", "advfirewall", "firewall", "show", "rule", "name=all"], capture_output=True, text=True, timeout=10)
        text = out.stdout
        hits = []
        blocks = []
        rules = text.split("Rule Name:")
        for block in rules[1:]:
            header = block.splitlines()[0].strip()
            lower = block.lower()
            action = "block" if "action" in lower and "block" in lower else "allow" if "action" in lower and "allow" in lower else "unknown"
            for p in ports:
                if str(p) in block:
                    hits.append((p, header, action))
                    if action == "block":
                        blocks.append(p)
        return hits, blocks
    except Exception:
        return [], []

def hosts_blocking(domains):
    try:
        path = os.path.join(os.environ.get("SystemRoot", r"C:\\Windows"), r"System32\\drivers\\etc\\hosts")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            data = f.read().lower()
        blocked = [d for d in domains if d.lower() in data]
        return blocked
    except Exception:
        return []

def copy_logs_to_clipboard():
    try:
        text = "```\n" + "\n".join([l for l in LOG_LINES if l.strip()]) + "\n```"
        subprocess.run(["clip"], input=text, text=True, check=False)
        return True
    except Exception:
        return False

def installed_vpn_clients():
    names = []
    keys = [
        (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
        (winreg.HKEY_CURRENT_USER, r"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
        (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
    ]
    patterns = ["vpn", "wireguard", "openvpn", "anyconnect", "nordvpn", "protonvpn", "surfshark", "expressvpn", "windscribe", "hide.me", "forticlient"]
    for root, path in keys:
        try:
            with winreg.OpenKey(root, path) as k:
                i = 0
                while True:
                    try:
                        sub = winreg.EnumKey(k, i)
                        i += 1
                    except OSError:
                        break
                    try:
                        with winreg.OpenKey(k, sub) as sk:
                            name = winreg.QueryValueEx(sk, "DisplayName")[0]
                            if name:
                                low = name.lower()
                                if any(p in low for p in patterns):
                                    names.append(name)
                    except OSError:
                        pass
        except OSError:
            pass
    return list(dict.fromkeys(names))

def vpn_web_check():
    try:
        url = "http://ip-api.com/json/?fields=status,message,query,isp,org,as,proxy,hosting"
        with urllib.request.urlopen(url, timeout=6) as resp:
            data = json.loads(resp.read().decode("utf-8", errors="ignore"))
        if data.get("status") != "success":
            return False, None
        proxy = bool(data.get("proxy"))
        hosting = bool(data.get("hosting"))
        isp = data.get("isp") or "unknown"
        org = data.get("org") or "unknown"
        return (proxy or hosting), {"isp": isp, "org": org, "proxy": proxy, "hosting": hosting}
    except Exception:
        return False, None

def main():
    enable_vt_mode()
    set_title("Rusticaland Server Checker")
    set_fixed_console()
    clear_console()
    if not is_admin():
        cprint("Warning", "Program is not running on Administrator priviledges")
    net_ok, _ = check_network()
    delay()
    cprint("Info", "Checking VPN/Proxy")
    installed = installed_vpn_clients()
    if installed:
        cprint("Warning", f"Installed VPN clients detected: {', '.join(installed)}")
    proxy_on, proxy_detail = detect_proxy()
    vpn_on = detect_vpn()
    if proxy_on:
        msg = f"Proxy detected ({proxy_detail})" if proxy_detail else "Proxy detected"
        cprint("Warning", msg)
    if vpn_on:
        cprint("Warning", "VPN adapter detected")
    if not proxy_on and not vpn_on:
        cprint("Success", "No VPN/Proxy detected")
    delay()
    cprint("Info", "Checking external VPN/Proxy status")
    vpn_ext, info = vpn_web_check()
    if info:
        if vpn_ext:
            cprint("Warning", f"External check suggests VPN/Proxy/Hosting (ISP {info['isp']}, Org {info['org']})")
        else:
            cprint("Success", f"External check: No VPN/Proxy detected (ISP {info['isp']}, Org {info['org']})")
    delay()
    web_ok, _ = check_website()
    delay()
    servers = [
        ("rusticaland.com", 28022, "Rusticaland Vanilla"),
        ("rusticaland.com", 28098, "Rusticaland Sandbox"),
        ("rusticaland.com", 28053, "Rusticaland OneGrid"),
        ("rusticaland.com", 28015, "Rusticaland Monthly"),
        ("rusticaland.com", 28066, "Rusticaland Modded"),
    ]
    results = []
    for h, p, name in servers:
        ok = check_server_flow(h, p, name)
        results.append(ok)
        delay()
        print("")
    if (not web_ok) or (not all(results)):
        cprint("Info", "Checking firewall blocks")
        fw_on = firewall_state()
        if fw_on:
            ports = [28022, 28098, 28053, 28015, 28066, 28016, 28023, 28099, 28054, 28067]
            hits, blocks = firewall_rules_ports(ports)
            if hits:
                for p, name, action in hits[:10]:
                    cprint("Warning" if action == "block" else "Info", f"Firewall rule '{name}' references port {p} ({action})")
            else:
                cprint("Success", "No explicit firewall rules for Rusticaland ports found")
            blocked_hosts = hosts_blocking(["rusticaland.com", "rusticaland.net"])
            if blocked_hosts:
                cprint("Warning", f"Hosts file may block: {', '.join(blocked_hosts)}")
        else:
            try:
                tcp_ping("google.com", 443, timeout=3)
                cprint("Warning", "Destination may be blocked by ISP or remote firewall")
            except Exception:
                cprint("Warning", "Firewall may be blocking rusticaland.net and game ports")

    print("")
    print("Click X to exit")
    print("Click C to copy console input")
    while True:
        ch = input("> ").strip().upper()
        if ch == "X":
            break
        if ch == "C":
            ok = copy_logs_to_clipboard()
            if ok:
                cprint("Success", "Console output copied to clipboard for Discord")
            else:
                cprint("Error", "Could not copy to clipboard")
        else:
            cprint("Info", "Press X to exit or C to copy")

if __name__ == "__main__":
    main()

