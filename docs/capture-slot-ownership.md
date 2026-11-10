# Pending screenshot slot ownership

Normal debug screenshots use two readback buffers, selected by present parity.
A pending capture may outlive the next non-shot present when the GPU is behind.
Another shot two presents later selects the same buffer. Submitting that new copy
before reading the old capture lets a fast GPU replace the pixels, even though
the later read waits for the older timeline value. The filename/flash analysis
then identifies the old frame while consuming the new frame's bytes.

Before recording a new copy into a pending capture's slot, snapshot that capture
into CPU-owned pixels. Its existing consumer moves the snapshot instead of reading
the reused buffer. Another slot still follows the asynchronous path, and no
pending capture adds no wait. Re-arming starts with no saved snapshot, so the old
pixels cannot be attached to the new frame. The generated-shot path and capture
format/metadata remain unchanged.

`do_present` is serialized by the replay job consumer; synchronous replay calls
hold `sync_order_mu`. The pending state remains in that producer/consumer flow.
There is at most one pending snapshot, with the same pixel allocation the normal
consumer already makes, moved earlier only when slot reuse requires it.

`tests/test_capture_slot_ownership.py` compiles the actual copy-selection, snapshot,
read and pending-consume statements with a queued GPU model. It covers a deferred
capture across a non-shot frame, same/opposite parity, fast/late new completion,
no-pending captures and re-arming across sparse intervals. It checks pixels and
metadata together, and verifies that different slots do not wait before submit.
This models legal GPU ordering; it does not claim to reproduce a hardware race.

This change concerns buffer lifetime. Host-cache visibility is the separate
`invalidate_mapped` contract tracked in PR1502; if that change lands first, retain
its normal-shot invalidation inside `shot_pixels`, after the wait and before the
copy into CPU-owned storage. PR1025's on-demand path still needs the same lifetime
rule when integrated; request paths do not change readback ownership.
