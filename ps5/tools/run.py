"""PS5: one test run of the title on the console, ended by the title's own exit.

    python3 ps5/tools/run.py NAME [--seconds 120] [--env 'KEY=VALUE' ...] [--deploy]

--deploy uploads what changed in dist/PPSA99024 first. The run's switches go to /app0/hookshot_test_env.txt, which the
title reads in place of a player's hookshot_env.txt and with none of its limits (host/hos_main.c), and which is taken
away when the run ends: a player's start never sees a test's input script. PS5_Vulkan's run tool launches the title and
captures klog; after --seconds this puts /app0/quit-run.txt in the title folder, and the title leaves through the shell
(host/ps5/win32_ps5.c quit_watch). The run ends early on the title's exit or a crash record. klog and the game's own
log land in work/runs/NAME.*; a crash's addresses are symbolised against the linked ELF.

The console: PS5_HOST (default 192.168.50.115, my Pro).
"""
import argparse
import ftplib
import io
import os
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
VULKAN = os.environ.get("PS5_VULKAN", os.path.join(ROOT, "..", "PS5_Vulkan"))
os.environ.setdefault("PS5_HOST", "192.168.50.115")
sys.path.insert(0, os.path.join(VULKAN, "tools"))
TITLE = "PPSA99024"
APP = "/data/homebrew/" + TITLE


def ftp():
    f = ftplib.FTP()
    f.connect(os.environ["PS5_HOST"], 2121, timeout=30)
    f.login()
    f.voidcmd("TYPE I")
    return f


def put(path, data):
    f = ftp()
    f.storbinary("STOR " + path, io.BytesIO(data))
    f.quit()


def get(path):
    f = ftp()
    b = bytearray()
    try:
        f.retrbinary("RETR " + path, b.extend)
    except ftplib.all_errors:
        return None
    finally:
        f.close()
    return bytes(b)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("name")
    ap.add_argument("--seconds", type=float, default=120)
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--deploy", action="store_true")
    a = ap.parse_args()
    runs = os.path.join(ROOT, "work", "runs")
    os.makedirs(runs, exist_ok=True)
    if a.deploy:
        r = subprocess.run([sys.executable, os.path.join(VULKAN, "tools", "deploy-title-folder.py"), "--always", "eboot.bin",
                            os.path.join(ROOT, "dist", TITLE)], capture_output=True, text=True)
        print((r.stdout + r.stderr).strip().splitlines()[-1] if (r.stdout + r.stderr).strip() else "deployed", flush=True)
        if r.returncode:
            return r.returncode
    put(APP + "/hookshot_test_env.txt", ("\n".join(a.env) + "\n").encode())
    f = ftp()
    for stale in ("/ps5_samples.txt",):
        try:
            f.delete(APP + stale)
        except ftplib.all_errors:
            pass
    try:
        f.delete(APP + "/quit-run.txt")
    except ftplib.all_errors:
        pass
    f.quit()

    import importlib.util
    spec = importlib.util.spec_from_file_location("run_title", os.path.join(VULKAN, "tools", "run-title.py"))
    rt = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(rt)
    started = [None]
    stop_sent = threading.Event()

    def stopper():
        while started[0] is None:
            time.sleep(0.2)
        while time.time() - started[0] < a.seconds:
            time.sleep(0.5)
            if stop_sent.is_set():
                return
        put(APP + "/quit-run.txt", b"stop\n")
        stop_sent.set()
        print("run: %.0f s: quit-run.txt sent" % a.seconds, flush=True)

    def on_line(line):
        if started[0] is None:
            started[0] = time.time()
        if "[ps5]" in line or "FATAL" in line or "CRASH" in line:
            print(line.rstrip()[:300], flush=True)

    threading.Thread(target=stopper, daemon=True).start()
    log = os.path.join(runs, a.name + ".klog")
    ended, _, _ = rt.run_title(TITLE, r"\[ps5\] exit|fault in the fault handler", a.seconds + 90, log, (), "",
                         os.path.join(ROOT, "work", "ps5", "link", "llvm-pie.elf"), on_line=on_line)
    stop_sent.set()
    try:   # (the run's switches go with the run)
        g = ftp()
        try:
            g.delete(APP + "/hookshot_test_env.txt")
        except ftplib.error_reply as e:   # (this server answers a delete with 226, which ftplib takes for an error)
            if not str(e).startswith("226"):
                raise
        g.quit()
    except ftplib.all_errors as e:
        print("run: hookshot_test_env.txt is still on the console (%s): remove it before playing" % e, flush=True)
    samples = get(APP + "/ps5_samples.txt")   # (HOS_PS5_SAMPLE: ps5/tools/samples.py names them)
    if samples is not None:
        open(os.path.join(runs, a.name + ".samples"), "wb").write(samples)
    # the game's log is in klog on the console (standard error: host/hos_main.c), each line after the title's prefix
    game = b""
    for line in open(log, "rb"):
        at = line.find(b"[Hookshot] ")
        if at >= 0:
            game += line[at + 11:]
    open(os.path.join(runs, a.name + ".log"), "wb").write(game)
    print("run %s: %s; klog %s, game log %s" % (a.name, ended, log,
                                                  "%d lines" % game.count(b"\n") if game else "none"), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
