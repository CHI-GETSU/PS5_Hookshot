# Frame-log sink failures

The optional binary frame log is ready only after its image, three mapped readback
buffers, output file and writer thread are available. A failed startup disables
logging for that process, reports the failing stage and releases the newly created
resources and file. It does not announce successful recording or retry every frame.

The worker checks both record writes and flushes. A short write or failed flush
disables further capture, discards queued CPU records, reports the error and closes
the file once. Enqueue checks the same state under the queue lock, so a producer
arriving after failure cannot leave records behind a worker that has exited.
Readbacks already in flight retain their GPU resources for the existing process
lifetime; they are not destroyed underneath submitted work.

Successful output keeps the existing record format, per-record flush and512record
queue bound. After a failed write the last record may be incomplete: the strict
reader rejects truncation rather than treating the file as a complete comparison.
This does not add renderer shutdown/draining or guarantee durability at process
exit; the healthy writer retains its existing process-lifetime behavior.

`tests/test_framelog_sink.py` executes production startup/cleanup, worker and enqueue
code. Only GPU/IO calls and the startup thread factory are stand-ins. Cases cover
allocation and missing-map failures, file/thread creation failure, repeated ready
queries, real CPU-thread short-write/flush failures, late producers, unchanged
record bytes and queue bounds. Tests require no game or GPU.
