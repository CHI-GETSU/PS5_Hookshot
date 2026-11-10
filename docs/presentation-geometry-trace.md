# Effective presentation geometry diagnostics

Set `RX_VK_PRESENT_LOG=1` before launching to log presentation geometry changes.
Unset it or set it to `0` to disable it. This writes ordinary diagnostic lines;
it does not enable frame-log image readback or change rendering/settings.

Each sample reports texture size, supplied crop request, clipped content size,
swapchain output size, the actual destination rectangle, observed host aspect,
sampled-view availability, and the route selected by presentation:

- `direct`: the source blit was selected, including fallback when filtering returned no image.
- `scaled`: the scaling function returned an image and its result was selected.
- `clear`: presentation had no source and selected its black clear.
- `hdr`: the HDR conversion function returned an image selected for presentation.
  A failed converter falls back to the scaled/direct route. Native PQ input bypasses conversion.

Filter, sharpness and frame-generation values are labeled `*-request`: configuration
does not establish that a feature ran successfully. The route is a CPU command-recording
decision, not a successful GPU present result. The host-aspect field reuses the exact
aspect passed to the destination fit, without a second callback read. No-source frames
report it unavailable, since fitting deliberately does not query the callback then.
This diagnostic does not alter aspect policy.
An unavailable/invalid aspect is explicit; invalid values are normalized for stable deduplication.
Requests accompany geometry/route changes but do not themselves trigger a line, so dragging
a sharpness slider cannot consume the geometry-change budget while the effective route stays the same.

Repeated unchanged samples are suppressed independently for game, interpolated (`made`)
and startup/clear frames, so alternating ordinary/interpolated sources does not spam a
line per frame. Up to 256 geometry changes are logged per process, followed by one
suppression notice. Restart to reset the limit. Continuously changing dynamic resolution
can reach that limit; this is a bounded diagnostic, not a full timeline capture.

`tests/test_present_geometry_trace.py` executes the production presentation-route and
trace blocks with CPU command spies. It checks effective crop/rectangle/output/options,
resize, direct fallback/scaled/clear routes, independent frame kinds, disabled/zero
configuration, invalid aspects and the output bound. No GPU or game data is required.
