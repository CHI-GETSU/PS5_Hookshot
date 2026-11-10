"""PS5: /app0/ps5_samples.txt (HOS_PS5_SAMPLE) named: each thread's commonest call chains, the busiest threads first.

    python3 ps5/tools/samples.py work/runs/NAME.samples [--threads 8] [--chains 5] [--only NAME]

A chain is the sampled instruction then its callers, innermost first; "(kernel)" is a system library. Lifted game
functions are f_<guest address>. The addresses are eboot offsets, symbolised against the linked ELF.
"""
import argparse
import collections
import os
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("samples")
    ap.add_argument("--threads", type=int, default=8)
    ap.add_argument("--chains", type=int, default=5)
    ap.add_argument("--only")
    ap.add_argument("--elf", default=os.path.join(ROOT, "work", "ps5", "link", "llvm-pie.elf"))
    a = ap.parse_args()
    rows = []
    for line in open(a.samples):
        parts = line.split()
        if parts[2] == "other":
            continue
        rows.append((int(parts[0]), parts[1], tuple(parts[2:])))
    addrs = sorted({x for _, _, ch in rows for x in ch if int(x, 16) < 0x7f0000000})
    out = subprocess.run(["llvm-symbolizer", "--obj=" + a.elf, "-f", "-C", "--no-inlines"], input="\n".join(addrs),
                         capture_output=True, text=True).stdout.split("\n\n")
    name = {x: (b.strip().splitlines() or ["?"])[0] for x, b in zip(addrs, out)}
    per = collections.defaultdict(collections.Counter)
    for n, th, ch in rows:
        named = []
        for x in ch:
            fn = name.get(x, "(kernel)")
            if not named or named[-1] != fn:
                named.append(fn)
        per[th][" < ".join(named)] += n
    threads = sorted(per.items(), key=lambda kv: -sum(kv[1].values()))
    if a.only:
        threads = [kv for kv in threads if kv[0].startswith(a.only)]
    for th, c in threads[:a.threads]:
        total = sum(c.values())
        print("%s: %d samples" % (th, total))
        for chain, n in c.most_common(a.chains):
            print("  %5.1f%%  %s" % (100.0 * n / total, chain))


if __name__ == "__main__":
    main()
