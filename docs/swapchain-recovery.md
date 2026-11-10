# Failed swapchain replacement

Passing a non-null `oldSwapchain` to `vkCreateSwapchainKHR` retires it even if
creation fails. It cannot be acquired again or passed as a non-retired old chain
on the next attempt. See the [Vulkan swapchain creation contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainCreateInfoKHR.html).

The same restriction applies when creation succeeds but enumerating the new
images or creating their presentation semaphores fails. Leaving the retired
handle in `dev.swapchain` makes the following present use an invalid chain.

Replacement now drains outstanding work with the existing device/present-queue
idle helper, invalidates Reflex's cached handle, and releases the retired chain
and its per-image semaphores after the creation attempt. New images and
semaphores are prepared before publishing the replacement. An unsuccessful
image-data query, including `VK_INCOMPLETE`, discards the incomplete replacement.

At the start of presentation, failed recreation returns before advancing the
frame slot, waiting/resetting its fence, or acquiring an image. The requested
size is retained for another attempt unless the window has already published a
newer size. A missing swapchain also triggers recreation without requiring a
new window event. Failures before the creation attempt leave the existing chain
owned but skip that presentation and retry the request.

## Regression coverage

`python -I -B tests/test_present_semaphores.py --cxx clang++` compiles extracted
creation, presentation-prefix, semaphore-selection and Reflex-invalidation code
with CPU Vulkan stubs. It covers:

- Four swapchain images across three frame slots, followed by two/five-image replacements.
- Retirement even when `vkCreateSwapchainKHR` fails.
- Semaphore allocation failure, image-count error/zero, and incomplete image data.
- Release of partial resources and recovery on a later attempt.
- A failed preflight retaining the current chain while skipping acquisition.
- Retrying the requested size while preserving a newer resize during recreation.
- Recreation requested by Reflex and recovery without a resize notification.
- Invalidation of Reflex's cached handle before its swapchain is destroyed.

The test requires a C++20 compiler, with no game, SDK runtime, GPU or display.
It is discovered by the existing source-regression runner. CPU stubs do not
establish driver behavior, Vulkan validation-layer results, or timing under
concurrent game rendering. Native WSI and game integration remain separate checks.

Reflex consumers check eligibility while holding their state mutex. A latency
sleep captures its swapchain generation and timeline target before releasing
that mutex for the CPU wait. After the wait, it checks the same generation,
latest target and eligibility before marking a new frame. Retired or replaced
chains, including a replacement reusing the same handle, cannot receive stale
markers. `tests/test_reflex_swapchain_lifetime.py` compiles the actual consumers
and exercises retirement before locking and during the wait, handle reuse,
overlapping sleep targets, disabling Reflex/turning on FG, and later recovery.

This handles replacement failure rather than device-loss or surface-loss
recovery. It preserves the existing sizing policy, per-image presentation
semaphores, queue locking, and frame-generation presentation path.
