# UI texture staging visibility

The custom ImGui texture uploader packs the requested rectangles into mapped
staging memory and flushes that allocation after its final pixel write, before GPU
use. Sequential-write host-visible memory need not be coherent. VMA's flush helper
handles cache-line alignment and treats coherent allocations appropriately.

This covers new font/logo textures, individual dirty rectangles and the fallback
update rectangle, for RGBA and Alpha8 expansion. The existing image layouts,
descriptor publication, retry behavior and staging retirement remain unchanged.
The bundled ImGui backend maintains its own vertex/index upload caches; this
corrects Hookshot's separate texture-upload path.

`tests/test_ui_texture_upload.py` executes production pixels/upload/retirement code
with the pinned ImGui/Vulkan types. A CPU/GPU cache model covers coherent and
noncoherent memory, full and partial uploads, untouched pixels, failure/retry and
deferred destruction. GPU copies execute after recording returns, so flushing
after recording but before submission remains a valid control. HDR color
conversion and physical noncoherent hardware behavior are not modeled.
