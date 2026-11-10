"""PS5: your game's romfs from its .nup straight to the title folder on the console, over the console's FTP server
(no 26 GB copy on the PC first). Each file is read from the package as nx2/nup.py extracts it and sent as it is read.

    python3 -I ps5/tools/upload-romfs.py <your game.nup> [--host 192.168.x.y] [--port 2121] [--to /data/homebrew/PPSA99024/romfs]

Resumable: what was sent is listed in .local/romfs-uploaded.txt and skipped next time. Three connections: the console's
FTP server writes about 7 MiB/s a connection and 22 MiB/s in all (PS5_PayloadSDK platform/docs/PROBE.md).
"""
import argparse
import ftplib
import io
import os
import queue
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "nx2"))
import nup as nupmod  # noqa: E402

DONE = os.path.join(ROOT, ".local", "romfs-uploaded.txt")


def connect(host, port):
    f = ftplib.FTP()
    f.connect(host, port, timeout=60)
    f.login()
    f.voidcmd("TYPE I")
    return f


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("nup")
    ap.add_argument("--host", default=os.environ.get("PS5_HOST"))
    ap.add_argument("--port", type=int, default=2121)
    ap.add_argument("--to", default="/data/homebrew/PPSA99024/romfs")
    ap.add_argument("--connections", type=int, default=3)
    a = ap.parse_args()
    if not a.host:
        sys.exit("upload-romfs: --host or PS5_HOST")
    pkg = nupmod.Nup(a.nup)
    t = nupmod.pick_title(pkg, None)
    files = [(rel, r) for rel, r in pkg.walk(t["romfs"])]
    for rel, _ in files:
        nupmod.extraction_path("/nonexistent-check", "romfs", rel)   # (the same path checks as an extraction)
    os.makedirs(os.path.dirname(DONE), exist_ok=True)
    done = set(open(DONE, encoding="utf-8").read().split("\n")) if os.path.exists(DONE) else set()
    todo = [(rel, r) for rel, r in files if rel not in done]
    total = sum(r[2] for _, r in todo)
    print("romfs: %d files, %d to send (%.2f GB) to %s:%s" % (len(files), len(todo), total / 1e9, a.host, a.to), flush=True)
    work = queue.Queue()
    for item in todo:
        work.put(item)
    lock = threading.Lock()
    sent = [0, 0]
    made = set()
    errors = []
    log = open(DONE, "a", encoding="utf-8")
    t0 = time.time()

    def mkdirs(f, path):
        parts = path.strip("/").split("/")
        for i in range(1, len(parts) + 1):
            d = "/" + "/".join(parts[:i])
            with lock:
                if d in made:
                    continue
            try:
                f.mkd(d)
            except ftplib.error_perm:
                pass   # (there already)
            with lock:
                made.add(d)

    def worker():
        f = connect(a.host, a.port)
        own = nupmod.Nup(a.nup)
        while True:
            try:
                rel, r = work.get_nowait()
            except queue.Empty:
                break
            dst = a.to.rstrip("/") + "/" + rel
            buf = io.BytesIO()
            own.copy_to(r, buf, rel)
            buf.seek(0)
            for attempt in range(3):
                try:
                    mkdirs(f, os.path.dirname(dst))
                    f.storbinary("STOR " + dst, buf, blocksize=1 << 20)
                    break
                except (ftplib.all_errors) as e:
                    if attempt == 2:
                        with lock:
                            errors.append((rel, str(e)))
                        break
                    time.sleep(2)
                    try:
                        f.close()
                    except Exception:
                        pass
                    f = connect(a.host, a.port)
                    buf.seek(0)
            with lock:
                if not errors or errors[-1][0] != rel:
                    log.write(rel + "\n")
                    log.flush()
                sent[0] += 1
                sent[1] += r[2]
                if sent[0] % 2000 == 0:
                    secs = time.time() - t0
                    print("  %d files, %.2f GB, %.1f MB/s" % (sent[0], sent[1] / 1e9, sent[1] / 1e6 / max(secs, 1)), flush=True)
        f.quit()

    threads = [threading.Thread(target=worker) for _ in range(a.connections)]
    for th in threads:
        th.start()
    for th in threads:
        th.join()
    print("romfs: %d files, %.2f GB in %.0f s, %d failed" % (sent[0], sent[1] / 1e9, time.time() - t0, len(errors)), flush=True)
    for rel, e in errors[:20]:
        print("  failed: %s: %s" % (rel, e))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
