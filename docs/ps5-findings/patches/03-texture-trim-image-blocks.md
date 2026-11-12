# 03 — top mip levels left out of huge compressed textures; image pool block size

- Applies to: Hookshot upstream 4cfa08f, `gpu/vk/vk_texture.cpp`, `gpu/vk/vk_vma.cpp`, new `gpu/vk/ps5_switches.h`.
- License: GPL-3.0-or-later (Hookshot's).
- Switches: `RX_VK_TEX_TRIM_MB=<n>` (a BC/ASTC, non-depth, single-sample texture with mips whose levels above the kept
  ones exceed n MB starts at a smaller level; absent: off); `RX_VK_IMAGE_BLOCK_MB=<8..512>` (absent: 128, as before).
- `ps5_switches.h` holds every switch's decision as small inline functions, so a PC test can check them. It also holds two
  helpers for the dedicated-image switch of FINDINGS §9 (`ps5_image_dedicated_bytes`, `ps5_image_goes_dedicated`); the
  code that used them is NOT in these patches (it crashed the console).
- Measured gain: FINDINGS §7a. Test: `../tests/test_fin.cpp` covers the header's later functions; the trim decision was
  tested by our own unit test (not included: it depends on our build tree).
