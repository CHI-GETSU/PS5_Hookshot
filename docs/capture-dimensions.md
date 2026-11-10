# Screenshot dimension validation

`RX_VK_SHOT_W` and `RX_VK_SHOT_H` accept positive decimal integers. Missing variables
retain the1280x720 default. Empty, signed, whitespace-containing, partially numeric,
zero and overflowing values disable screenshots for that process and report the
invalid request once. They do not fall back to a different requested capture size.

Before any screenshot allocation, the dimensions must fit the device's
`maxImageDimension2D`, the RGBA readback's CPU addressable size, and the existing
PNG encoder. That encoder writes one IDAT with stored deflate blocks: the checked
size includes its zlib header, five bytes per block and Adler32. The
[PNG chunk limit](https://www.w3.org/TR/png-3/#5Chunk-layout) is2^31-1 bytes.
This is a format/capability bound; it does not add a memory budget or guarantee
that a valid request can be allocated on a particular GPU.

Rejected requests never enter the capture producer. Window presentation continues;
headless presentation keeps its existing offscreen policy and produces no capture
for an invalid size. The fixed-size binary frame logger is independent. Valid
standard, ultrawide, odd and small dimensions retain their requested sizes.

`tests/test_capture_dimensions.py` compiles production setup, the real capture
entry/presentation conditions, and the PNG writer. A Vulkan allocation model checks
rejection before calls, reports once, and preserves window/headless routing. A
small real PNG validates chunk CRCs, decoded scanlines and stored-block sizing.
Large dimension/format boundaries use handles only, never large allocations.

Resource readiness in #1472 remains separate. In a combined source tree its sliced
setup test must include the actual dimension guard and initialized physical-device
limits before exercising allocation failures; it must not stub the guard away.
