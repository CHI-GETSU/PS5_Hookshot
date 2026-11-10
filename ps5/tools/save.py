"""PS5: the game's save folder kept and put back, so test runs start from the same autosave.
    python3 ps5/tools/save.py backup [DIR]     /app0/user/save to DIR (default work/save-backup)
    python3 ps5/tools/save.py restore [DIR]    DIR over /app0/user/save
"""
import ftplib, io, os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REMOTE = "/data/homebrew/PPSA99024/user/save"
cmd = sys.argv[1]
local = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "work", "save-backup")
f = ftplib.FTP(); f.connect(os.environ.get("PS5_HOST", "192.168.50.115"), 2121, timeout=30); f.login(); f.voidcmd("TYPE I")
n = 0
if cmd == "backup":
    def walk(r, l):
        global n
        os.makedirs(l, exist_ok=True)
        lines = []; f.retrlines("LIST " + r, lines.append)   # (no NLST on the console's server)
        for x in lines:
            name = x.split()[-1]
            if name in (".", ".."): continue
            if x.startswith("d"): walk(r + "/" + name, os.path.join(l, name))
            else:
                b = bytearray(); f.retrbinary("RETR " + r + "/" + name, b.extend); open(os.path.join(l, name), "wb").write(b); n += 1
    walk(REMOTE, local)
else:
    for d, _, files in os.walk(local):
        r = REMOTE + "/" + os.path.relpath(d, local).replace(os.sep, "/") if d != local else REMOTE
        try: f.mkd(r)
        except ftplib.all_errors: pass
        for name in files:
            f.storbinary("STOR " + r + "/" + name, io.BytesIO(open(os.path.join(d, name), "rb").read())); n += 1
f.quit()
print("%s: %d files (%s)" % (cmd, n, local))
