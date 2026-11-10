# Frame interpolation setup failures

Frame interpolation is optional. A failed pipeline, sampler, image or image-view
creation must leave it disabled for the session, with its unused resources released
and ordinary presentation still available. An unchecked sampler result must never
be advertised as successful initialization.

`vk_framegen.cpp` constructs each pipeline and image locally, publishing the complete
object only on success. Failure releases its intermediate resources. Interpolation
initialization also releases earlier successful pipelines/samplers when a later
stage fails. The separate window-scaling path uses the same pipeline constructor
but retains its own state.

Resize keeps the existing display-job drain and queue/device-idle sequence before
replacing images. If replacement fails, all newly allocated interpolation images,
its setup objects and the published size/multiplier/history are cleared. Size and
multiplier are committed only after every required image/view exists. Interpolation
keeps the existing permanent-failure policy; this adds no per-frame retry loop,
new setting, allocation budget or change to interpolation quality.
The disabled flag is atomic: the replay thread writes it, while Reflex's
game-thread eligibility check can read it through `framegen_on()`.

`tests/test_framegen_resources.py` compiles the real constructors and lifecycle
functions with CPU Vulkan stubs and checks each creation failure, ownership order,
no duplicate destruction, stale metadata, resize draining and the direct-presentation
gate. It covers fixed2x and max8x image sets and the independent scale initialization.
It needs Vulkan headers but no GPU, SDK vendor runtime or game assets.

Injected failures in these tests do not prove actual driver OOM recovery or
device-loss handling. A successful game smoke validates integration, not a fault
injection into the player's GPU.
