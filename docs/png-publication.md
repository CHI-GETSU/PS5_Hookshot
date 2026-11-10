# Complete PNG publication

Screenshot, flash and frame-dump writers can run concurrently. The PNG writer
previously built a shared CRC table without synchronization, ignored write/close
errors, and opened the destination directly. A failed capture could replace an
older complete file, while the normal capture still announced success.

The CRC table is now immutable after thread-safe local initialization. PNG sizes
are checked before encoding. The encoded data is written to an exclusively
created temporary in the destination directory; every write and the final close
must succeed before replacement. Failure preserves the prior destination, closes
the owned handle and attempts to remove only this call's temporary. Temporary
name collisions are skipped without truncating other files. Diagnostics identify
the phase, error code and path, with a bounded report count. A later capture can
retry normally. Only successful normal captures announce that they were written.

Each PNG is published independently; a flash triplet is not a multi-file
transaction. Same-directory replacement targets complete-file visibility on
supported local filesystems, not power-loss durability or network-share
transaction guarantees. The encoder's pixels, alpha omission, compression and
capture scheduling are unchanged. Host/renderer ABI is unchanged.

The game-free test compiles the real CRC/writer and normal success-announcement
body. Authored CRT/file replacement failures cover each write, close, publication,
cleanup and open; old destinations and unrelated colliding temporaries must
survive. Concurrent first use and competing writers produce independently decoded
PNG chunks, CRCs, zlib data and RGB pixels, including multiple stored blocks.
The existing capture dimension/PNG consumer check is retained with its new return
type and required headers. Synthetic failures are not a physical disk-fault claim.

References: [PNG specification](https://www.w3.org/TR/png-3/),
[exclusive fopen mode](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/fopen-wfopen?view=msvc-170),
[Windows replacement API](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexa).
Windows narrow paths continue to use the game's existing UTF-8 manifest policy.
