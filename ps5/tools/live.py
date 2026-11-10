"""PS5: steer a running test (ps5/tools/run.py with HOS_INPUT_LIVE=/app0/input.txt and RX_VK_SHOTS=/app0/shots).

    python3 ps5/tools/live.py prepare            empty input.txt and the shots folder on the console
    python3 ps5/tools/live.py press '0:A,2:A'    presses in HOS_INPUT_SCRIPT's syntax, timed from when the game reads them
    python3 ps5/tools/live.py shot [DIR]          the newest screenshot to DIR (default work/runs/live), its path printed
"""
import ftplib
import io
import os
import sys

HOST = os.environ.get("PS5_HOST", "192.168.50.115")
APP = "/data/homebrew/PPSA99024"
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def ftp():
    f = ftplib.FTP()
    f.connect(HOST, 2121, timeout=30)
    f.login()
    f.voidcmd("TYPE I")
    return f


def main():
    cmd = sys.argv[1]
    f = ftp()
    if cmd == "prepare":
        try:
            f.mkd(APP + "/shots")
        except ftplib.error_perm:
            lines = []
            f.cwd(APP + "/shots")
            f.retrlines("LIST", lines.append)   # (no NLST on the console's server)
            for name in (l.split()[-1] for l in lines if l.split()[-1].endswith(".png")):
                try:
                    f.delete(APP + "/shots/" + name)
                except ftplib.all_errors:
                    pass   # (the console's server answers a delete with 226, which ftplib takes for an error)
        f.storbinary("STOR " + APP + "/input.txt", io.BytesIO(b""))
    elif cmd == "press":
        old = bytearray()
        try:
            f.retrbinary("RETR " + APP + "/input.txt", old.extend)
        except ftplib.error_perm:
            pass
        f.storbinary("STOR " + APP + "/input.txt", io.BytesIO(bytes(old) + sys.argv[2].encode() + b"\n"))
        print("pressed:", sys.argv[2])
    elif cmd == "shot":
        out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "work", "runs", "live")
        os.makedirs(out, exist_ok=True)
        lines = []
        f.cwd(APP + "/shots")
        f.retrlines("LIST", lines.append)
        names = sorted(l.split()[-1] for l in lines if l.split()[-1].endswith(".png"))
        if not names:
            print("no screenshot yet")
        else:
            b = bytearray()
            f.retrbinary("RETR " + names[-1], b.extend)
            path = os.path.join(out, names[-1])
            open(path, "wb").write(b)
            print(path, "(%d shots on the console)" % len(names))
    f.quit()


if __name__ == "__main__":
    main()
