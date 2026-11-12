# Memory findings from a second native PS5 port of the same game

These findings come from a separate native PS5 port of *Ocarina of Time* (Switch 2) built on the Hookshot recompilation.
It uses your PS5_Mesa RADV (at f5610fe) and a copy of your PS5 Platform title heap. Tests were on a retail PS5, firmware 13.60,
from 2026-11-07 to 2026-11-11.
Our Hookshot base is upstream 4cfa08f (2026-11-08), so it is older than v2.0.6 and older than your fork's base.

Each section says what is **DEMONSTRATED** (measured on the console or reproduced in a PC test), what is a
**HYPOTHESIS** (consistent with the data, not proven) and what is **UNCERTAIN**.
"Candidate" means one console build that changes one thing against a reference build.
Every change is behind a switch read from a boot-time env file (boot.ini), and it is off by default.

Our direct-memory budget for reference: 12,128 MiB with `flexibleMemorySize` 544 MiB (see §5).

Several of these you have already found and fixed on your side (32 MB image blocks, heap segments kept apart,
operator new waiting). We list them anyway: independent numbers from another build may still help.

---

## 1. Host thread stacks: 16 MiB each, at most 141 KiB used

**Problem.** Every host thread got a 16 MiB stack of committed direct memory. About 92 threads are alive in play.

**DEMONSTRATED (console)**
- **Measurement:** a stack-depth report (`HOS_STACK_REPORT=1`) ran over 100 s in play. It showed 91–92 live stacks; the deepest used **141 KiB**, and all of them stayed under 256 KiB.
- **16 → 4 MiB:** about 1.1 GiB of direct memory back, with no overflow.
- **4 → 1 MiB:** **+267 MiB** more (forest test, about 7.5 min of play, no stack fault). That makes about 1.37 GiB in all against 16 MiB.
- **A trap we fell into:** the size must be read before the first `CreateThread`. Our boot.ini was parsed on the game thread, so the setting was silently ignored. It is now read before that (`HOS_PS5_STACK_MB`).

**Fix / switch.** `HOS_PS5_STACK_MB=1`.
Your fork already reserves the whole stack and backs 256 KiB that grows on fault, which is better. Our numbers only confirm that 256 KiB is plenty for this game.

---

## 2. Command streams: 24 MB each, peak use 55–70 KiB

**Problem.** Each recording slot had a 24 MB host-visible stream, and two of them shared one 64 MB VMA block.

**DEMONSTRATED (console)**
- **Peak use:** 55 KiB of 24 MB in one test, and 70 KiB of 2 MiB over 14,336 recordings in another. No stream ever filled.
- **Stream in its own memory** (no shared 64 MB block): 327–456 MiB less outside the game's own memory, at the same point of play.
- **48 streams of 2 MiB** (96 MiB in all): about **1,166 MiB** less again, at the same point of play. We expected 1,056 MiB.
- A full stream falls back without writing, and the overflow is logged (read in the code; never triggered on the console).

**Fix / switch.**
- `RX_VK_STREAM_MB=<1..24>` (default 2 in our build).
- `RX_VK_STREAM_REPORT=1` logs the peak use.
- Patch: `patches/02-command-streams.diff`.

**UNCERTAIN.** Whether 1 MB is safe in every area: we have only measured the title screen, the castle courtyard, Hyrule Field and Kokiri Forest.

---

## 3. Title heap in 2 MiB pieces against fragmented direct memory

**Problem.** The heap allocated each dlmalloc segment as one physically contiguous block of direct memory. One heap refusal happened with **183 MiB free, but a largest block of 53 MiB**; operator new then failed.

**Fix.** Each segment is taken in 2 MiB pieces, each allocated and mapped on its own at the next address. The CPU needs consecutive addresses, not consecutive physical memory.
- Switch: `HOS_HEAP_PIECE_MB` (default 2; 0 = original behaviour).
- Refusals are logged with `HOS_HEAP_REPORT=1`.

**DEMONSTRATED**
- The PC test passes (gcc, TSan).
- Later console runs that ran out of memory show refusals with "got 0". So the heap failed because nothing was left, not because of fragmentation.

**UNCERTAIN.** The gain on its own: no console run has yet failed in a way that only fragmentation would explain. Your SDK's main reportedly has 2 MiB segments too.

**Patch:** `patches/01-heap-pieces-gap-trim.diff`. It applies to your `platform/src/heap.c` at b83202be and is GPL-3.0-or-later, as your file is.

---

## 4. The dlmalloc heap never gives memory back: adjacent segments are merged

**Problem.** The heap places segments end to end in its range. dlmalloc merges a segment it maps right after another into one segment. It returns a segment only when the whole of it is free, and the partial unmap it tries from `sys_trim` is refused by `heap_unmap_locked`. So the mapped heap only ever grows.

**DEMONSTRATED**
- **Console:** across 11 logs, the mapped heap never fell by more than 64 MiB in all. Its peak was 784–912 MiB in Kokiri Forest with a warm shader cache, and 1,712–1,744 MiB in runs that compiled everything (cold cache, see §6).
- **PC, with the real heap.c and mspace.c and your `tests/host_kernel.c`:** we mapped 64 MiB and freed all of it. 64 MiB stayed mapped after `free`, and still after `mspace_trim`.
- **PC, with a free 64 KiB unit (address space only) after every segment:** dlmalloc keeps segments apart, and emptied ones go back. For 400 MiB of 100 KiB blocks, freed except one block in K:

  | Kept | No gap | With gap |
  |---|---|---|
  | 1 in 50 | 400 MiB | **400 MiB** (every segment still holds a block) |
  | 1 in 500 | 400 MiB | 144 MiB |
  | 1 in 4000 | 400 MiB | 32 MiB |
  | 8 threads churning + trim | 512 MiB | 144 MiB |

- TSan: no race in the changed code. The remaining reports are inside dlmalloc, whose spin locks TSan does not understand; they appear with and without the gap.

**HYPOTHESIS.** What it would return on the console: 0 to about 650 MiB in the forest, and up to about 1.4 GiB after a full shader compile. Long-lived objects scattered across segments can pin them all (the "1 in 50" row). An in-use counter (`HOS_HEAP_COUNT=1`) is built to settle this. It is not yet run on the console.

**Fix / switch.**
- `HOS_HEAP_GAP=1`: the gap, plus `ps5p_heap_trim()` (an `mspace_trim` of every arena) every 10 s.
- `HOS_HEAP_COUNT=1`: bytes in use.
- Patch 01 again.
- Your SDK 55a2de0 ("segments never joined, `ps5_heap_trim`") is the same idea. Our numbers are an independent confirmation.

---

## 5. flexibleMemorySize 768 → 544, and a SPIR-V "parse failure" that was not one

**DEMONSTRATED**
- `flexibleMemorySize` 768 → 544 MiB in param.json gave **+224 MiB** of direct memory (12,128 MiB in all). The title ran with 51 MiB of flexible memory still free.
- In three runs under memory pressure, RADV refused the same 19,542-word fragment shader 8 times. The error was `SPIR-V parsing FAILED: count >= 1 && w + count <= end` at byte 18,952, then "failed for good", and the title closed.
  - It happened with flexible memory at 768 and at 544, so the change of size was not the cause.
  - It happened with our pool report on or off, so the report was not the cause either.
- We then dumped the module. It was the same module 9 times, and `spirv-val` says it is **valid**. In a later run RADV accepted it.

**HYPOTHESIS.** The refusal came from memory running out while the module was copied or parsed, and it was reported as a parse error, not as out-of-memory. The same module, unchanged, parses when memory is available. We have not found the allocation that failed.
**What to watch for:** a SPIR-V parse error on the console, under low memory, may be an out-of-memory in disguise.

---

## 6. Shader cache: without it, the first start runs the heap out and crashes

**DEMONSTRATED (console).** After a system reinstall, RADV's shader cache and Hookshot's program/pipeline caches were gone, and the first start crashed:
- **Where:** in RADV, during a graphics pipeline compile, in `nir_copy_prop_vars` (`copy_entry_create`, under `radv_shader_spirv_to_nir`), because the heap was out of memory.
- **Heap peak:** 1,744 MiB with empty caches, 1,526 MiB with partly refilled caches, 1,318 MiB with warm caches. With empty caches, the heap climbed 560 → 1,744 MiB between 100 and 170 s of loading.

**Fix (procedure).** Back up the RADV cache directory and the Hookshot cache files after every test. Restore them before the first start after a reinstall.
Your note that replacing a large file in the title folder stalls every `open` for about 1.8 s matches the pipeline-cache rewrite we see.

---

## 7. Textures

### 7a. Leaving out top mip levels of huge compressed textures
**Problem.** Hyrule Field's terrain arrays are the biggest textures.

**DEMONSTRATED (console).**
- `RX_VK_TEX_TRIM_MB=<n>` leaves out the top levels of BC/ASTC, non-depth, single-sample textures whose top levels exceed n MB.
- **About 400 MiB** saved in the castle and field. The log's "852 → 53 MB" counts RGBA8; after BC3 re-encoding, each array is 213 MiB.
- No visible problem was reported by eye.

**Patch:** `patches/03-texture-trim-image-blocks.diff`. It also adds `RX_VK_IMAGE_BLOCK_MB`, the image pool's block size (we run 32, as you do).

### 7b. A lower texture budget made fragmentation worse
**DEMONSTRATED (console).** With the cache budget at 1,700 MB instead of 2,200, in the Kokiri Forest test:
- the image pool had 91 blocks (2,912 MiB) for 2,500 MiB of allocations, so it was 85 % full, with **514 holes and 412 MiB lost**. At 2,200 it was 99 % full with 137 holes;
- 301 textures were remade after eviction, 83 of them within 120 frames.

Evicting frees space *inside* blocks, and a block only goes back when it is wholly empty. Remade textures and the next area's textures do not fit the holes, so new blocks are made.
**Lesson:** evicting more does not return direct memory; emptying whole blocks does.

### 7c. BUG: a texture evicted, then remade, came back empty
**DEMONSTRATED (console log).**
- **The texture:** one 260×260 texture (NVN format 0xd) that had been rendered by the GPU.
- **Why it was evicted:** the rule that protects rendered textures expires after 600 or 3600 frames.
- **What went wrong:** the remake saw its storage as GPU-owned, so it did not upload it, and it was sampled **EMPTY**. Its texels existed only in the evicted image.

**Fix (PC-tested, NOT yet run on the console).**
- `RX_VK_EVICT_KEEP_RENDERED=1`: a texture the GPU wrote, not finalized by the game and not overwritten in guest memory, is never evicted, whatever its age.
- **Patch:** `patches/04-evict-keep-rendered-finalize-report.diff`. It needs `ps5_switches.h` from patch 03.

---

## 8. Area change: the old area's textures stay

**DEMONSTRATED (console, Kokiri Forest → next area).**
- **Steady forest:** 1,595 MB of textures (769–774 textures) under a 2,200 MB budget, with 1,073 MB free inside the image blocks.
- **During the transition:** 2,353 MB and 1,096 textures, over the budget, with 415 MB free in the blocks. No eviction was logged: eviction only takes textures unused for 60 frames, and the old area's were still recent.
- **Crash:** 70 MiB of direct memory free, then `operator new` failed (see §10).
- **Cause in the code:** `nvnMemoryPoolFinalize` (`host/nvn_native.c`) makes no call into the Vulkan back end. Textures in a pool the game has finalized leave only through the cache's eviction. `texture_released` only flags GPU-written textures and frees nothing.

**HYPOTHESIS.** Freeing the textures of finalized pools at once could cover the shortfall (up to about 750 MB). Only blocks that become wholly empty count, though (§7b).
A measurement is built: `RX_VK_FINALIZE_REPORT=1` reports every 10 s what is finalized and unused, and how many pool blocks would become wholly empty. It is in patch 04 and not yet run on the console.

**Also.** On our base the budget is forced by `quality_preset` (preset 1 gives 2,200), and `RX_VK_VRAM_MB` is ignored. Upstream #1376 fixes this on newer bases.

---

## 9. WARNING: dedicated image allocations crashed the whole console

**DEMONSTRATED.** One candidate allocated every image of **2 MiB or more** in memory of its own (VMA dedicated allocation; switch `RX_VK_IMAGE_DEDICATED_MB=2`).
- **What happened:** during a Kokiri Forest test, the whole PS5 crashed (not only the title). It restarted into safe mode and the system had to be reinstalled.
- **What was lost:** the title, its data folder and the logs of the run.
- **The PC test** (lavapipe) passed: 352 → 272 MB.

**UNCERTAIN: the cause was not found.**
- The obvious suspect is a GPU access to memory freed while still in use. But our RADV (PS5_Mesa f5610fe) already keeps a freed buffer's memory until the GPU has run what was submitted before.
- Reading the code, we found no use-after-free path.
- **Other candidates, all unproven:**
  - the rate of `sceKernelAllocateDirectMemory` / `ReleaseDirectMemory` / `Munmap` calls;
  - many small dedicated BOs in the RADV winsys;
  - an interaction with sparse images.

**Why we tell you.** Your fork gives images of 1 MB and up memory of their own when memory is short (commit "images of 1 MB and up get memory of their own while memory is short"). Your scripted Hyrule Castle Town run passed, but your commit says the rule was barely exercised there. Please be careful before long runs where it is active. A console-wide crash also costs a system reinstall.

---

## 10. operator new waiting for memory

**DEMONSTRATED (console).**
- The area-change crash is SIGILL in our `operator new`: a `ud2` right after `malloc` returns NULL (the C++ runtime has no exceptions).
- At that moment, 70 MiB of direct memory was free.

**Fix (same idea as your "operator new waits for memory instead of ending the title").**
- `HOS_NEW_WAIT_MS=<0..60000>`: on a refusal, trim the heap and retry every 20 ms up to that long, logging the start of the wait, every second, and the end. Then trap as before.
- PC test: gcc and clang UBSan, TSan (3 passes, no warning), 8 threads waiting at once.
- We do not ship this patch: our operator new lives in a runtime file that is not useful to you, and you already have the same mechanism.

**Not tested on console.** The project has stopped, so this test will not take place. Only the PC tests above were run.

---

## Also worth knowing (from reading, not from tests)
- **PS5_Mesa f5610fe → 0f86253 is two commits:** the cached mesh pipeline that lost a stage, and the 128 MiB mesh scratch ring. The 0f86253 message says a shader that keeps more than an entry writes into the next wave's entry: corruption without any error, only a line on stderr at compile time.
- **NotRexed/Hookshot#817 (Steam Deck, RDNA2):** one task shader, NVN code `2c56dc1006942f74`, runs for seconds and gets the GPU reset. `RX_VK_NO_MESH=1` avoids it. We run with `RX_VK_NO_MESH=1` because of a GPU hang with mesh draws on the console. Whether it is the same shader is UNCERTAIN.
