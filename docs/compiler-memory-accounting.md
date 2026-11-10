# Compiler memory accounting

The build memory gate counts a live compiler driver and its descendants, rather
than just the small driver process. `rss_mb` returns zero for a process not yet
attached, unavailable counters, or a root whose `Popen.poll()` already reports
completion. Retaining a Popen/OS handle must not let stale process-tree counters
or a subsequently reused PID contribute memory after that root has ended.

This check precedes the platform counter query. It does not change compiler peak
estimates, worker floors, reserves, lease release or live child accounting. A
process can still exit during a later process-tree snapshot; this is not an atomic
snapshot or a complete PID-ancestry race solution.

`tests/test_rss_liveness.py` retains an actual completed Popen and models a stale
positive backend sample. The known exit takes precedence without querying that
backend; live-tree sampling and unavailable-counter fallback are also checked.
`test_mem_gate_children.py` separately checks a real child holding300MiB, and the
existing memory-gate/lease regressions cover scheduling behavior. These tests need
no compiler, game dump or game execution.
