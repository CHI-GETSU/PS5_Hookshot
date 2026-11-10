"""A replay read reuses the answers for the last pages it read until a stamp or a new capture.

Once a capture exists, every uniform word took the pool lock to ask whether its page
was in one. With one page kept, reads that took turns between two pages (a draw's
stages' uniform buffers) copied a whole captured page each time: four are kept, the
oldest replaced first, and RX_VK_SUBMITTED_PAGES=1 keeps one.
python -I -B tests/test_submitted_page.py [--cxx g++]
"""
import argparse
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def block(text, marker):
    start = text.index(marker)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cxx", default="g++")
    a = p.parse_args()
    source = (ROOT / "gpu" / "vk" / "vk_memory.cpp").read_text(encoding="utf-8")
    fn = block(source, "bool submitted_page_reuse(")
    ways = block(source, "int submitted_ways(")
    find = block(source, "SubmittedPage* submitted_find(")
    slot = block(source, "SubmittedPage& submitted_slot(")
    if "submitted_page_reuse(" not in find:
        raise SystemExit("submitted_find does not ask submitted_page_reuse")
    read = block(source, "bool memory_read_as_submitted(")
    reuse_at = read.index("submitted_find(")
    lock_at = read.index("std::lock_guard lk(pools_mu)")
    if reuse_at > lock_at:
        raise SystemExit("the page is looked up under the pool lock before the cache")
    for name in ("void memory_stamp(", "void memory_collect_written(", "bool take_capture("):
        body = block(source, name)
        if "cap_view_bump()" not in body:
            raise SystemExit(f"{name} changes page state without dropping the cached read")
    fixture = f'''#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#define CHECK(x) do {{ if (!(x)) {{ std::fprintf(stderr, "FAIL:%d %s\\n", __LINE__, #x); std::exit(1); }} }} while (0)
struct SubmittedPage {{ uint64_t page = ~0ull; uint32_t pgen = 0; uint64_t view = 0; bool overlay = false; }};
constexpr int kSubmittedPages = 4;
thread_local SubmittedPage submitted_pages[kSubmittedPages];
thread_local unsigned submitted_next;
{fn}
{ways}
{find}
{slot}
static void keep(uint64_t page, uint32_t gen, uint64_t view) {{
    SubmittedPage& e = submitted_slot();
    e.page = page, e.pgen = gen, e.view = view, e.overlay = true;
}}
int main() {{
    if (submitted_ways() == 1) {{   // (RX_VK_SUBMITTED_PAGES=1: one page, as before)
        keep(1, 1, 7), keep(2, 1, 7);
        CHECK(!submitted_find(1, 1, 7) && submitted_find(2, 1, 7));
        std::printf("PASS: RX_VK_SUBMITTED_PAGES=1 keeps one page\\n");
        return 0;
    }}
    // (two pages read in turn stay kept; a fifth page replaces the oldest)
    keep(1, 1, 7), keep(2, 1, 7);
    for (int i = 0; i < 6; i++) CHECK(submitted_find(1, 1, 7) && submitted_find(2, 1, 7));
    keep(3, 1, 7), keep(4, 1, 7), keep(5, 1, 7);
    CHECK(!submitted_find(1, 1, 7) && submitted_find(2, 1, 7) && submitted_find(5, 1, 7));
    CHECK(!submitted_find(2, 2, 7) && !submitted_find(2, 1, 8));   // (a stamp, a new capture: not reused)
    CHECK(submitted_page_reuse(9, 4, 7, 9, 4, 7));
    CHECK(!submitted_page_reuse(9, 4, 8, 9, 4, 7));
    CHECK(!submitted_page_reuse(9, 5, 7, 9, 4, 7));
    CHECK(!submitted_page_reuse(10, 4, 7, 9, 4, 7));
    CHECK(!submitted_page_reuse(9, 4, 7, 9, 4, 0));
    std::printf("PASS: a captured page is reused only for that generation\\n");
}}
'''
    with tempfile.TemporaryDirectory(prefix="submitted-page-") as tmp:
        cpp = Path(tmp) / "fixture.cpp"
        exe = Path(tmp) / "fixture"
        cpp.write_text(fixture, encoding="utf-8")
        subprocess.run([a.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)], check=True)
        env = {k: v for k, v in os.environ.items() if k != "RX_VK_SUBMITTED_PAGES"}
        subprocess.run([str(exe)], check=True, env=env)
        subprocess.run([str(exe)], check=True, env=dict(env, RX_VK_SUBMITTED_PAGES="1"))


if __name__ == "__main__":
    main()
