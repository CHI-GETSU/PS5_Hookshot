# 01 — title heap: 2 MiB pieces, a gap between segments, trim, in-use count

- Applies to: PS5 Platform `platform/src/heap.c` at b83202be (2026-09-28). `patch -p1` from the SDK's root.
- License: GPL-3.0-or-later (your file; the changes are under the same license).
- Switches (the title calls these once at start, before the threads that allocate):
  - `ps5p_heap_set_piece(bytes)`: each segment's direct memory in pieces of that size (0 = one block, as before).
    We pass `HOS_HEAP_PIECE_MB` (default 2).
  - `ps5p_heap_set_gap(bool)`: one free 64 KiB unit (address space only) after each segment, so dlmalloc never merges
    them. We pass `HOS_HEAP_GAP` (default 0).
  - `ps5p_heap_set_count(bool)` and `ps5p_heap_in_use()`: bytes in use (usable sizes). We pass `HOS_HEAP_COUNT` (default 0).
  - `ps5p_heap_trim()`: `mspace_trim` on every arena made so far. Returns what was unmapped (a figure for the log). We call it
    every 10 s when the gap is on, and from operator new while it waits.
  - `ps5p_heap_refusal(...)`: the last direct-memory refusal (for a report).
- Test: `../tests/test_heap_gap.c`, built against `src/dlmalloc/mspace.c`, `src/placement.c` and `tests/host_kernel.c`:
  `cc -std=gnu11 -pthread -Iinclude -Isrc -Itests src/heap.c src/placement.c tests/host_kernel.c test_heap_gap.c mspace.o`
  then `./a.out 0 500` (no gap) and `./a.out 1 500` (gap). Results in FINDINGS §4.
- Overlap: your SDK 55a2de0 already keeps segments apart and adds `ps5_heap_trim`; this is the same idea, done independently.
