# 04 — never evict a texture whose texels exist only on the GPU; finalized-texture report; pool report

- Applies to: Hookshot upstream 4cfa08f after patch 03 (needs `ps5_switches.h`): `gpu/vk/vk_texture.cpp`,
  `gpu/vk/vk_texture.h`, `gpu/vk/vk_vma.cpp`.
- License: GPL-3.0-or-later (Hookshot's).
- Switches (all off by default):
  - `RX_VK_EVICT_KEEP_RENDERED=1`: the eviction skips a texture with `gpu_written && !released && !overwritten`,
    whatever its age (the bug in FINDINGS §7c).
  - `RX_VK_FINALIZE_REPORT=1`: every 10 s, on the replay thread: textures finalized by the game in the last 10 s, those
    finalized and unused since, and how many VMA memory blocks hold only those (would go back whole if they were freed).
    Read only.
  - `vma_pool_report_ps5()`: how full the image pool's blocks are (blocks, allocations, holes, largest hole); our title
    calls it every 10 s with `RX_VK_POOL_REPORT=1`. This is the report behind "514 holes" in FINDINGS §7b.
- Test: `../tests/test_fin.cpp` (`g++ -std=c++20 -fsanitize=address,undefined`): the decision functions and the
  per-block tally. ALL OK with gcc and clang. Not yet run on the console.
