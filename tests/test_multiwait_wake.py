"""A WaitAny is woken only by an object some holder links into a multi-wait (host/nn_os_sync.c poke_multiwait). The actual
functions (the watched-address table, poke_multiwait, holder_watch, Link/Unlink/UnlinkAll/MoveAll) are compiled with a
harness that counts WakeAllConditionVariable and every object lookup. Every change anywhere woke every WaitAny before.

python -I -B tests/test_multiwait_wake.py [--cxx clang++]
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def block(source, marker):
    start = source.index(marker)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


HARNESS = r"""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef long LONG;
typedef struct { int x; } SRWLOCK;
typedef struct { int x; } CONDITION_VARIABLE;
#define SRWLOCK_INIT {0}
#define CONDITION_VARIABLE_INIT {0}
static int lock_depth, lookups_locked, target_lookups, wakes;
static int fail_next_realloc;
static void *test_realloc(void *p, size_t n) {
    if (fail_next_realloc) { fail_next_realloc = 0; return NULL; }
    return realloc(p, n);
}
#define realloc test_realloc
static LONG InterlockedCompareExchange(volatile LONG *p, LONG x, LONG c) { LONG v = *p; if (v == c) *p = x; return v; }
static void AcquireSRWLockExclusive(SRWLOCK *l) { (void)l; lock_depth++; }
static void ReleaseSRWLockExclusive(SRWLOCK *l) { (void)l; lock_depth--; }
static void WakeAllConditionVariable(CONDITION_VARIABLE *cv) { (void)cv; wakes++; }
typedef struct Obj {
    uint64_t key, event; int sem_max, kind; struct Obj **links; int nlinks, caplinks; struct Obj *mw;
} Obj;
enum { K_EVENT = 1, K_TIMER, K_SEMA, K_QUEUE, K_MULTIWAIT, K_HOLDER };
HOLDER_ENUM
static Obj objs[128];
static Obj *obj(uint64_t key, int kind) {   /* (the real one is a scheduling point; a target lookup can resurrect it) */
    if (lock_depth) lookups_locked++;
    if (key < 8) target_lookups++;
    objs[key].key = key; objs[key].kind = kind;
    return &objs[key];
}
typedef struct { uint64_t x[8]; } Ctx;
#define ARG(i) (c->x[i])
#define HLE(name) static void name(Ctx *c)
static SRWLOCK mw_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE mw_cv = CONDITION_VARIABLE_INIT;
static volatile LONG mw_waiting;
FUNCTIONS
static int bad;
static int count(uint64_t k) { int i = mw_watched_at_locked(k); return i < 0 ? 0 : mw_watched[i].n; }
static void expect(const char *step, int e1, int e2) {
    if (count(1) != e1 || count(2) != e2) {
        printf("FAIL after %s: event1 watched by %d (want %d), event2 by %d (want %d)\n", step, count(1), e1, count(2), e2);
        bad = 1;
    }
}
static int poked(uint64_t k, int waiting) { mw_waiting = waiting; wakes = 0; poke_multiwait(&objs[k]); return wakes; }
static void call(void (*f)(Ctx *), uint64_t a, uint64_t b) { Ctx c = {{a, b}}; f(&c); }
int main(int argc, char **argv) {
    const int all = argc > 1;   /* (HOS_MW_WAKE=all set by the caller) */
    const uint64_t A = 10, B = 11, h1 = 20, h2 = 21, h3 = 22, h4 = 23;
    objs[1].key = 1; objs[2].key = 2;
    objs[h1].event = 1; objs[h1].sem_max = H_EVENT;   /* (two holders over event 1, one over event 2, one over a thread) */
    objs[h2].event = 1; objs[h2].sem_max = H_EVENT;
    objs[h3].event = 2; objs[h3].sem_max = H_EVENT;
    objs[h4].event = 3; objs[h4].sem_max = H_THREAD;
    if (poked(1, 1) != all) { puts("FAIL: an unwatched change woke WaitAny"); bad = 1; }
    for (uint64_t h = h1; h <= h4; h++) call(LINK, A, h);
    expect("four links", 2, 1);
    if (poked(1, 1) != 1 || poked(2, 1) != 1) { puts("FAIL: a watched change did not wake WaitAny"); bad = 1; }
    if (poked(5, 1) != all) { puts("FAIL: an unwatched change woke WaitAny with holders linked"); bad = 1; }
    if (poked(1, 0)) { puts("FAIL: a wake with no WaitAny waiting"); bad = 1; }
    call(UNLINK, h2, 0);
    expect("unlink one", 1, 1);
    call(UNLINK, h2, 0);
    expect("unlink it again", 1, 1);
    memset(&objs[1], 0, sizeof objs[1]);   /* (event 1 initialized again, as fresh() does, its holder still linked) */
    objs[1].key = 1;
    if (poked(1, 1) != 1) { puts("FAIL: a re-initialized watched event no longer wakes WaitAny"); bad = 1; }
    call(MOVEALL, B, A);
    expect("move all", 1, 1);
    if (objs[h1].mw != &objs[B] || objs[A].nlinks) { puts("FAIL: move all did not move the holders"); bad = 1; }
    objs[2].kind = 0;   /* (event 2 finalized before its holder is unlinked) */
    call(UNLINKALL, B, 0);
    expect("unlink all", 0, 0);
    if (objs[2].kind) { puts("FAIL: unlink brought a finalized event back"); bad = 1; }
    call(LINK, A, h1);
    call(UNLINKALL, B, 0);
    expect("link again, unlink all of the other", 1, 0);
    call(UNLINK, h1, 0);
    expect("unlink the last", 0, 0);
    if (mw_nwatched) { printf("FAIL: %d addresses still watched\n", mw_nwatched); bad = 1; }
    /* Keep the holder array allocated so the injected failure hits the watched-address table. */
    objs[40].links = (Obj **)realloc(NULL, 32 * sizeof *objs[40].links); objs[40].caplinks = 32;
    for (uint64_t k = 1; k <= 17; k++) {
        objs[k].key = k; objs[64 + k].event = k; objs[64 + k].sem_max = H_EVENT;
        if (k <= 16) call(LINK, 40, 64 + k);
    }
    fail_next_realloc = 1;
    call(LINK, 40, 81);
    if (fail_next_realloc || poked(17, 1) != 1) { puts("FAIL: failed watch registration lost a wake"); bad = 1; }
    call(UNLINK, 65, 0);   /* free a table slot, then link a second holder over address 17 */
    objs[82].event = 17; objs[82].sem_max = H_EVENT;
    call(LINK, 40, 82);
    call(UNLINK, 81, 0);
    if (poked(17, 1) != 1) { puts("FAIL: unlink of an uncounted holder hid another live holder"); bad = 1; }
    call(UNLINKALL, 40, 0);
    if (mw_nwatched) { puts("FAIL: watches remain after OOM-case cleanup"); bad = 1; }
    if (target_lookups) { printf("FAIL: %d lookups of a watched object (link, unlink, poke look up none)\n", target_lookups); bad = 1; }
    if (lookups_locked) { printf("FAIL: %d lookups (scheduling points) under mw_lock\n", lookups_locked); bad = 1; }
    puts(bad ? "FAILED" : "ok");
    return bad;
}
"""


def fixture(source):
    table = source[source.index("typedef struct { uint64_t key; int n; } MwWatched;"):source.index("/* o: the object that changed")]
    parts = [table, block(source, "static void poke_multiwait("), block(source, "static uint64_t holder_watch(")]
    defines = []
    for alias, name in (("LINK", "LinkMultiWaitHolder"), ("UNLINK", "UnlinkMultiWaitHolder"),
                        ("UNLINKALL", "UnlinkAllMultiWaitHolder"), ("MOVEALL", "MoveAllMultiWaitHolder")):
        marker = re.search(r"HLE\((_ZN2nn2os\d+%sE\w*)\)" % name, source)
        parts.append(block(source, marker.group(0)))
        defines.append("#define %s %s" % (alias, marker.group(1)))
    enum = re.search(r"enum \{ H_EVENT[^}]*\};", source).group(0)
    return HARNESS.replace("HOLDER_ENUM", enum).replace("FUNCTIONS", "\n".join(parts + defines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="clang++")
    parser.add_argument("--source", type=Path, default=ROOT / "host/nn_os_sync.c")
    args = parser.parse_args()
    source = args.source.read_text(encoding="utf-8")

    calls = re.findall(r"poke_multiwait\(([^)]*)\);", source)   # (every change passes the object that changed)
    assert calls and all(re.fullmatch(r"\w+", c) for c in calls), "a poke_multiwait call without the object: %r" % calls

    compiler = shutil.which(args.cxx)
    if not compiler:
        raise SystemExit("C++ compiler not found: " + args.cxx)
    with tempfile.TemporaryDirectory(prefix="multiwait-wake-") as temporary:
        folder = Path(temporary)
        (folder / "fixture.cpp").write_text(fixture(source), encoding="utf-8")
        exe = folder / ("fixture.exe" if os.name == "nt" else "fixture")
        subprocess.run([compiler, "-std=c++17", "-O1", "-w", str(folder / "fixture.cpp"), "-o", str(exe)], check=True,
                       timeout=120)
        env = os.environ.copy()
        for mode in ("targeted", "all"):
            env.pop("HOS_MW_WAKE", None)
            argv = [str(exe)]
            if mode == "all":
                env["HOS_MW_WAKE"] = "all"
                argv.append("all")
            result = subprocess.run(argv, env=env, capture_output=True, text=True, timeout=30)
            print(mode + ":", result.stdout.strip())
            if result.returncode:
                raise SystemExit("FAIL: " + mode)
    print("PASS: WaitAny woken only for watched addresses (HOS_MW_WAKE=all: every change); link, unlink, unlink all and move all"
          " keep the counts by address; a re-initialized watched event still wakes; finalize-before-unlink resurrects nothing;"
          " no watched-target lookups or lookups under the extracted paths' mw_lock; allocation failure preserves wakes")


if __name__ == "__main__":
    main()
