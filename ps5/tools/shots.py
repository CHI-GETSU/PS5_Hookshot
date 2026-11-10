"""PS5: the run's screenshots from the console (RX_VK_SHOTS=/app0/shots) into a folder.
    python3 ps5/tools/shots.py DIR [every]      every: each n-th plain shot (default 1); flash_* shots always
"""
import ftplib, os, sys
out = sys.argv[1]; every = int(sys.argv[2]) if len(sys.argv) > 2 else 1
os.makedirs(out, exist_ok=True)
f = ftplib.FTP(); f.connect(os.environ.get("PS5_HOST", "192.168.50.115"), 2121, timeout=30); f.login(); f.voidcmd("TYPE I")
f.cwd("/data/homebrew/PPSA99024/shots"); lines = []; f.retrlines("LIST", lines.append)
names = sorted(l.split()[-1] for l in lines if l.split()[-1].endswith(".png"))
plain = [n for n in names if n.startswith("vk_")]
for n in [n for n in names if not n.startswith("vk_")] + plain[::every]:
    b = bytearray(); f.retrbinary("RETR " + n, b.extend); open(os.path.join(out, n), "wb").write(b)
f.quit()
print(len(names), "on the console;", ", ".join(plain[::every]))
