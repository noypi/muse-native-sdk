"""Per-phase startup breakdown labels (CORE-012 labels only).

This module owns the phase names used by ``benchmark.startup.run`` so the
phase-attributed trace has a single edit point. Timings themselves are
produced by the harness; this module defines the ordered labels.
"""

# Ordered phase labels: mmap snapshot, AOT bytecode, bootstrap.
PHASES = (
    "mmap_snapshot",
    "aot_bytecode",
    "bootstrap",
)
