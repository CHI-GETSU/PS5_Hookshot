# host/: the standalone runtime

A fork of the Horizon OS layer of NotRexed's a64-kit (`hos/`, TOTKPort commit `e88744a`, 2026-10-31), the runtime
that runs Tears of the Kingdom natively. It replaces the Nintendo SDK at the `nn::` boundary: an
`HLE(<mangled name>)` function here stands in for that SDK function wherever the game calls it
(`nx2/standalone.py` picks them up at lift time). The rest of the SDK runs as lifted code.

Taken unmodified except as noted:
`hos.h`, `hos_main.c`, `hos_mem.c`, `hos_thread.c`, `hos_svc.c`, `hos_hostcall.c`, `nn_os_sync.c`, `nn_fs.c`,
`nn_hid.c`, `nn_libc.c`, `nn_diag.c`, `nn_applet.c`, `nn_audio.c`, `nvdrv.c`.

mkw-recomp's changes:
- **The guest window.** It is 64-bit, 512 GB, reserved whole. The image sits at Eden's `0x80000000`, the address the
  code was lifted for. The heap starts at 64 GB, and address regions grow down from 512 GB (`hos.h`). The runtime
  commits what it hands out (`hos_commit`), and `mkw_glue.c` commits anything else on first touch. TOTK's whole
  4 GB window was committed up front.
- `hos_main.c`: the image base comes from `gen/hos_image.c`.
- `hos_mem.c`: the default memory size is a Switch 2 application's 9809 MB.
- `hos_svc.c`: MKW's program id, and the code region at the image base.
- **Graphics.** `nvn_native.c` is TOTKPort's native NVN layer for NVN2: the Switch 2 driver's device limits (read by
  running its nvnDeviceGetInteger under Unicorn), GPU addresses over the 64-bit window (identity below 1 TB, virtual
  pools above), program builders, native images, sparse tile layouts; procs it lacks log their first call and answer
  0 (`HOS_NVN_STRICT=1`: stop, as in TOTKPort). `display.c` is TOTKPort's window without its emulated-GPU fallback.
  The replay was `gpu/render` (see `gpu/PROVENANCE.md`); it is the Vulkan back end in `gpu/vk` now, with `gpu/render`
  as the fallback (`host/rx_backend.c`). `nn_vi.c` is MKW's own, with the window's frame pacing.
- **Virtual cores** (`hos_thread.c`): on by default with six cores; a waiter's request reaches a holder at its next
  loop header too (`mkw_glue.c` `a64_poll`), and equal-priority holders yield after a 2 ms slice.
- **Left out:** files bound to TOTK or its Maxwell GPU: `gpu.c`, `nvn_trace.c`, `hos_prof.c`, `mods.c`, `movie*.c`,
  `nn_image_debug.c`, `totk_*.c`. `mkw_glue.c` stands in for the hooks the taken files call into them. `mods.c`
  came back later as Hookshot's `host/mods.c`: the RomFS part of Skybound's loader (`NotRexed/SkyboundTotk`, MIT).

Licensing follows a64-kit: GPL, through suyu's translator in the lifted code (see
`third_party/a64-kit/NOTICE.TOTKPort.md`).
