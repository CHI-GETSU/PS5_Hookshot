# 02 — command streams: own memory, size by switch, use report

- Applies to: Hookshot upstream 4cfa08f, `gpu/vk/vk_draw.cpp`, `gpu/vk/vk_internal.h`, `gpu/vk/vk_vma.cpp`. `patch -p1` from
  the Hookshot root. On a newer base the hunks will need adapting (line offsets, possibly the RecordingRes layout).
- License: GPL-3.0-or-later (Hookshot's).
- Switches: `RX_VK_STREAM_MB=<1..24>` (2 by default in this patch; out of range: 2, logged); `RX_VK_STREAM_REPORT=1`
  (each recording's use, the peak, logged). A full stream is counted and logged (first 16, then each 1024th).
- Each stream is a host-visible buffer in a VkDeviceMemory of its own (`create_buffer_dedicated`), not two to a 64 MB VMA block.
- Measured gain: FINDINGS §2. Test: console only (no PC unit test for this one).
