# Frame dump readback visibility

Frame dumps are diagnostic inputs: their statistics, images and raw float data
must describe the completed GPU copy. `vk_dump.cpp` allocates mapped staging
buffers with `create_buffer`, which does not require a host-coherent memory type.
Waiting for completion alone does not establish every memory-domain/cache step.

Each dump copy records a COPY/TRANSFER_WRITE to HOST/HOST_READ dependency. The
existing replay caller flushes/waits its submission outside an open burst before
calling `dump_write`. The consumer then invalidates the copied allocation range
before reading pixels or storage-buffer words. VMA handles non-coherent atom
alignment and makes invalidation a no-op where host coherence permits it.

Typed color copies contain RGBA32 float, unsigned or signed integer texels.
Depth and RGB9E5 copies retain their original dimensions and two/four-byte packed
texels until invalidation; depth conversion may then downscale or replace the
mapped pointer with temporary float output. Invalidating after that conversion
would already be too late and could use the wrong range. Buffer dumps invalidate
their actual capped, word-aligned copied size, including the final word.

This changes only the opt-in dump path. Existing GPU barriers, submission waits,
allocation policy, file layout, numeric conversions and cleanup are preserved.
It does not make PNG/raw files a single atomic transaction or change generic
device-loss/wait-error handling. The readback contract assumes successful GPU
completion, as the existing dump caller does.

`tests/test_dump_readback.py` compiles the actual dump translation unit, actual
full-barrier implementation and dump consumer call into an authored CPU model.
Its 56 scenarios cover coherent/non-coherent caches, explicit/permissive host
visibility, typed and packed formats, downscaled depth, buffer tail, raw float
data, statistics-only output, open-burst deferral and cleanup. Copy data, waits,
cache behavior and Vulkan calls are modeled; PNG encoding is outside this test.
This is protocol/numeric regression coverage, not proof of physical non-coherent
hardware, driver behavior, campaign rendering or a performance improvement.

References: [Khronos readback example](https://docs.vulkan.org/guide/latest/synchronization_examples.html#cpu-read-back-of-data-written-by-a-compute-shader),
[Vulkan memory domains](https://docs.vulkan.org/spec/latest/appendices/memorymodel.html),
[VMA mapped-memory cache management](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/memory_mapping.html).
