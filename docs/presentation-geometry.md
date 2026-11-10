# Presentation geometry

The texture allocation, its active crop, the display aspect of that picture and
the swapchain size are separate inputs. `gpu/vk/vk_present_rect.h` is the pure
geometry contract used by the Vulkan presentation path.

- Without a valid `PresentAspect`, fit the clamped crop's pixel aspect, preserving
  the existing integer rounding and invalid-source fallback.
- A positive, finite `PresentAspect` describes the **presented picture after
  crop**, not the texture allocation. It can differ from the allocation's aspect:
  a wide camera picture in a 16:9 texture must still fill its intended wide window.
- Fit that display aspect into the current destination and center any bars. Round
  the logical fit to the nearest pixel; a float camera ratio must not introduce a
  one-pixel bar when the destination already has its intended shape. A nonempty
  destination retains at least one pixel per axis at extreme ratios.
- Zero, negative, NaN and infinite overrides fall back to the physical content
  aspect. The callback is read once per present; no host/backend ABI field is added.

For example, a 43:18 picture fills 1720x720. In a 1920x1009 destination it occupies
1920x804, with 102 pixels above and 103 below, even if the physical source texture
is 1920x1080. Changing the destination does not change the camera or render targets.

Normal and interpolated frames share `present_image`. Interpolation materializes
the crop into a separate image, but keeps the same display aspect. The scaling
filter receives the content rectangle's dimensions; the blit uses that same
rectangle and the existing clear path covers its bars. The Insert overlay remains
in swapchain coordinates. Internal screenshots retain their separate capture
dimensions and are not post-presentation evidence of letterboxing.

Game-free checks:

```sh
python -I -B tests/run_all.py present_rect test_present_aspect.py output_resolution --strict
```

`present_rect_test.cpp` covers fit, crop, invalid values, float rounding and bounded
centered rectangles across extreme sizes. `test_present_aspect.py` executes the
production geometry/blit-selection block with recording command stubs: host aspect,
normal/generated images, filtered/direct blits, bar clears and restore. It does not
execute Vulkan; backend compilation and a synthetic GPU/window integration run are
separate validation layers.

An optional GPU check (Vulkan 1.3 loader/device and `HOOKSHOT_VULKAN_HEADERS` needed):

```sh
python -I -B tests/run_all.py present_aspect_vulkan --strict
```

It executes the production transfer/present-selection block offscreen, reads the
pixels back and checks content and black bars against fixed expected rectangles.
Cases include matching wide output, maximize/restore geometry, a padded active crop
and invalid aspect fallback. The padded-crop check excludes only the scaled
half-source-texel footprint of linear filtering at content edges; every bar pixel
is checked. It does not validate swapchain acquisition, a window/DPI transition,
the interpolation shader or the upscaler. Ordinary CI skips this hardware check.
