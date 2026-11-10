# PNG flash sampling

`RX_VK_FLASH` compares a normal screenshot against its two neighbors using a64x36
RGB grid. Each cell covers its normalized source rectangle, including remainder
rows and columns when the capture dimensions do not divide the grid evenly.
Sampling retains the existing four-pixel stride. Its mean divides by the actual
number of visited samples, so a partial stride cannot brighten or wrap a channel.
Captures smaller than the grid repeat a valid source pixel for an otherwise empty
cell. Valid capture dimensions are positive; this does not change allocation or
dimension-validation policy.

The cell/region detector, thresholds and output filenames are unchanged. This is
a correction to diagnostic brightness, not a change to game rendering, FOV or HUD.
For example, a constant white3440x1440 capture used to become18 in the first grid
cell after overflow into uint8; its sampled channels now stay255. A below-threshold
brightness variation must not become a flash through that wrap.

`tests/test_flash_sampling.py` compiles the actual sampling and downstream detector
code with synthetic RGBA images. It checks constant channels at standard, wide,
odd and small sizes, labels source regions independently to detect partition drift,
and exercises stable images, a moving edge, a real returning brightness pulse and
a below-threshold negative. No game, GPU or image library is needed.
