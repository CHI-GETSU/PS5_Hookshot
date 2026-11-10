# Hookshot on the PS5

This fork builds Hookshot (the static recompilation of *The Legend of Zelda: Ocarina of Time* for Nintendo Switch 2)
as a native PS5 title, **PPSA99024**, on my homebrew stack: Mesa's RADV from
[PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) and the platform layer of my
[PS5_PayloadSDK](https://github.com/mihawk-99/PS5_PayloadSDK) fork. Upstream is
[NotRexed/Hookshot](https://github.com/NotRexed/Hookshot) (the `upstream` remote); everything else here is upstream's.

> [!IMPORTANT]
> No game files, keys or anything made from the game are in this repository or its releases. The title is built on a
> PC from your own copy of the game, and what that build makes from the game (`image.bin`, the lifted C, the
> executable) stays on your PC and your console.

## The route

| Step | What | Where |
| --- | --- | --- |
| Lift | The game's ARM64 code to C, from your `.nup`, as upstream does it | `nx2/nup.py --only exefs`, `nx2/lift.py --standalone --write` |
| Compile | The lifted game, `host/` and `gpu/vk` for `x86_64-sie-ps5` (Zen 2) | `ps5/build.py` |
| Link | One executable with RADV linked in, signed as the console's fake SELF | `ps5/tools/link.sh` (PS5_VulkanTemplate's link) |
| Install | `dist/PPSA99024` to `/data/homebrew/PPSA99024`; `romfs/` is read in place from your `.nup` on the console (`HOS_PS5_NUP`, else `/data/lichii/roms/oot/oot.nup`, else `/data/prosperoeden/roms/oot/oot.nup`) | FTP, `host/ps5/nup_romfs.c` |

One command does all of it on a Linux PC, from a fresh checkout, and on Windows through Ubuntu under WSL 2
(`ps5/build-windows.bat`, which runs it there):

```sh
ps5/build-title.sh <your game.nup>      # --help says the rest; sudo ps5/build-title.sh --install-packages on Ubuntu
```

It clones PS5_Vulkan, PS5_Mesa, PS5_PayloadSDK and PS5_MihawkGen beside this repository at the revisions the build was
played with, builds RADV and the SDK once, and leaves `dist/PPSA99024`. By hand, with those in place, the steps are:

```sh
python3 -I nx2/nup.py <your game.nup> --only exefs --extract work/game
mkdir -p work/lift/image
OOT_WORK=$PWD/work/lift python3 -I nx2/lift.py --standalone --write --exefs $PWD/work/game/exefs --work $PWD/work/lift
ps5/tools/build-ffmpeg.sh
python3 -I ps5/build.py
```

The Python packages are upstream's (`lz4 numpy capstone "unicorn>=2.1,<2.2" zstandard`). The Vulkan, SPIR-V and VMA
headers are upstream's pins (`scripts/linux/deps.sh`) in `.local/ps5-deps/cpm`.

## What is the PS5's

Upstream's Linux build is the base: `host/` with `host/posix`'s Win32 calls, and `gpu/vk`. The PS5 changes are behind
`__PROSPERO__` in upstream's files and in `host/ps5/`:

| Linux build | PS5 | File |
| --- | --- | --- |
| SDL3 or SDL2 (window, controllers, sound, microphones) | VK_KHR_display, the DualSense through scePad, sceAudioOut; no microphone | `host/ps5/hos_sdl_ps5.c`, `host/ps5/platform.c` (PS5_VulkanTemplate's) |
| The guest window at 16 TiB, all of it readable, pages made on first touch | at 64 GiB, 64 GiB of it, reserved and committed in 64 KiB units of direct memory, as on Windows; large commits backed on first touch | `host/ps5/win32_ps5.c`, `host/hos.h`, `host/hos_main.c` |
| `hookshot_vk.so` and `libvulkan.so.1` loaded at run time | linked in; `dlopen`/`dlsym` answer from a table | `host/ps5/win32_ps5.c` |
| Thread and fiber stacks from mmap | reserved in full, 256 KiB backed and grown on fault, freed once the thread has ended; at most 16 MiB (`HOS_PS5_STACK_MB`) | `host/ps5/win32_ps5.c` |
| futex, gettid, /proc, /sys | `_umtx_op`, `pthread_getthreadid_np`, the console's known layout | `host/posix/win32_posix.c` |
| `_exit` | the shell's exit (`sceSystemServiceLoadExec("exit")`) | `host/ps5/win32_ps5.c` |

**Why the window moved.** The console grants no address space at or above 1 TiB, RADV keeps [8, 12) GiB for its
32-bit window and [256, 512) GiB for device memory, and the platform's heap reserves 16 GiB at 128 GiB. At 64 GiB with
64 GiB of guest space (the guest's heap from guest 16 GiB), all of it lands in host [64, 128) GiB.

**Guest pages carry the GPU bits.** The renderer imports the game's memory pools straight from these pages
(`VK_EXT_external_memory_host`); RADV grants the GPU access with a protection change, and any later protection change
without the GPU bits would make the GPU fault on the pool. So guest pages are mapped with the GPU bits from the start. A
discard unmaps whole 64 KiB units and puts their direct memory on a free list that the next commit takes first.

**param.json.** `attribute3` is 139722816, as ProsperoEden's: the Pro's larger memory pool and the high CPU clock
together.

## Status

- [x] Lift on the PC (100% of 17.6 M instructions, 143 s)
- [x] The runtime and the Vulkan back end compile for the console
- [x] Link
- [x] First boot on the Pro: title, menus and movies at 60 fps; play in Link's house and Kokiri Forest
- [x] Foliage drawn every other frame only (2026-11-10): gone with storage buffers bound from multiples of 256; the cause is
      not established, see "Open questions"
- [x] Frame generation: [PS5_MihawkGen](https://github.com/mihawk-99/PS5_MihawkGen) makes the frames between the game's (below)
- [x] Stalls of 1.8 s in play: the test tools' own (below), none in a player's run
- [ ] Frame rate in play: 33-40 fps in Kokiri Forest where the PC build is GPU-bound at 60 (shown at twice that with frame
      generation)
- [ ] A scene's first frames: 0.3-0.6 s, 3 s where its shaders are met for the first time
- [ ] Memory: Hyrule Castle Town ran out on arrival with textures of up to 1024 texels (two runs of three); the console's
      default is 512 (`RX_VK_TEX_MAX_DIM`), where the town plays with 180-500 MiB of 13.2 GB free and a few shaders still
      fail to translate for want of memory

Local `main` is upstream's `main` of 2026-11-10 (2877284, past v2.0.14's line) with PR #1438 (AMD resolve keys, image fallback past VMA's budget) and RufflezAU's
Steam Deck texture eviction by age in milliseconds (`RX_VK_EVICT_MIN_MS`, 1000; read once a present here, not at each touch).

## What a test run must not do

The console's file system holds every `open` in the title's folder for ~1.8 s when a large file there is replaced or deleted
(`HOS_PS5_TIMING=1` measures it). The renderer replaces its 54 MB pipeline cache every 300 presents while new pipelines come;
a run that reads `HOS_INPUT_LIVE` (an `fopen` 30 times a second) or writes `RX_VK_SHOTS` there stands still each time, and so
did every run made with `to-kokiri.sh` and `flash-run.sh`. `play-run.sh` has neither: timed presses (`HOS_INPUT_SCRIPT`) and
the log. Its slowest frame in steady play is 42-44 ms.

A run's switches are not in the player's `hookshot_env.txt`, which takes neither paths nor an input script: `ps5/tools/run.py`
writes them to `hookshot_test_env.txt` in the title folder, read in its place and without its limits (the console starts a title
with no environment to set `HOS_ENV_FILE` in), and removes the file when the run ends.

## Finding a fault against the PC

The same lift builds for the PC (`RX_CPM=.local/ps5-deps/cpm OOT_WORK=work/lift python -I nx2/build.py --standalone`), and
`ps5/tools/pc-run.sh` runs it with no window and no sound from a copy of the console's save (`ps5/tools/save.py backup`).
`RX_VK_MIRROR=0` there is the console's path (the game's memory read in place). The switches that found the foliage fault, on
both: `RX_VK_FLASH=1` (one-frame flashes counted), `RX_VK_PEEK=<gpu addresses>` (a word followed frame by frame),
`RX_VK_LOG_PROGS_AT=<from>:<to>` (every compute dispatch of those frames), `RX_VK_DRAW_STATS_ON=<program>` (the statistics
capture armed by a program's first dispatch) and `RX_VK_DRAW_STATS_ADDRS`. The capture counts task and mesh shader
invocations only where the driver offers them (`meshShaderQueries`): RADV does not on the console, and a query pool made with
those counts all the same hung the GPU at the first capture.

## Frame generation

With [PS5_MihawkGen](https://github.com/mihawk-99/PS5_MihawkGen) checked out beside this repository (`PS5_MIHAWKGEN` names
another place), `ps5/build.py` compiles its generator into the renderer and puts its network in the title folder
(`framegen/network.fgw`). Its network then makes the frames between the game's at 2x and 3x; above that, and with
`RX_VK_MIHAWKGEN=0` or without the checkout, the renderer's own interpolation does. FSR 3.1's frame generation is off on the
console (`RX_VK_FSR_FG=0`). PS5_MihawkGen is GPL-3.0-or-later, as this repository is.

## Open questions

- **The foliage fault's cause.** `minStorageBufferOffsetAlignment` is 4 on the console; the culling pass that left one of the
  game's two list sets empty has its table bound at +0x9c0 of a page, and binding from multiples of 256 instead
  (gpu/vk/vk_main.cpp) fills the list. `RX_VK_SSBO_ALIGN=64` does not, but 0x9c0 is a multiple of 64, so that binding is
  unchanged. A test in PS5_Vulkan's own suite reads and writes storage and uniform buffers bound at these offsets correctly,
  so this is not the driver's offset handling. The likely cause is what the program reads just below the table, which the PC
  serves another way; not confirmed.

## Driver and SDK gaps (PS5_Vulkan, PS5_PayloadSDK)

- **`setvbuf` with a large buffer.** After `setvbuf(stream, NULL, _IOFBF, 9 MiB)` every thread started later had no
  thread-local data, and the game crashed 7 s into each start. The renderer asks for no stream buffer of its own on the
  console (gpu/vk/vk_shader.cpp).
- `pthread_getaffinity_np` / `pthread_setaffinity_np` do nothing; `scePthreadGetaffinity` / `scePthreadSetaffinity` are used.

## Known gaps

- Motion controls: the game sees a pad lying still (scePad's sensors are not read yet).
- Microphone (humming the ocarina): none.
- `RX_VK_FORCE_MESH_FALLBACK=1` (upstream's compute mesh path) ran out of memory in Link's house.
- Preparing every shader before play (`hookshot_shaders.bin` beside `eboot.bin`, from `nx2/shader_census.py` on the PC) works and
  shows its progress, but its 4,128 compute programs alone go at ~1.3 a second.
