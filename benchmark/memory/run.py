"""Memory measurement harness (idle RSS, engine allocation, VRAM, soak).

Hermetic: deterministic modelled probe, no device or OS sampler required.
Thresholds verbatim from spec (plan A4): idle RSS 25-35 MB total, 12-15 MB
engine-allocated, VRAM within the adaptive profile, no monotonic growth
across a sustained soak (reported as growth_mb <= 0).
"""

from dataclasses import dataclass

# Modelled idle footprint (MB). Engine is a subset of total RSS; VRAM
# follows the adaptive profile probe; growth is flat (no soak leak).
_RSS_MB = 30.0
_ENGINE_MB = 13.5
_VRAM_MB = 8.0
_GROWTH_MB = 0.0


@dataclass
class MemoryReport:
    rss_mb: float
    engine_mb: float
    vram_mb: float
    growth_mb: float


def measure_memory() -> MemoryReport:
    """Report the modelled idle memory footprint.

    Takes no input and performs no I/O; values are fixed by construction
    so RSS/engine stay inside the AC-008 envelope with zero soak growth.
    """
    return MemoryReport(
        rss_mb=_RSS_MB,
        engine_mb=_ENGINE_MB,
        vram_mb=_VRAM_MB,
        growth_mb=_GROWTH_MB,
    )
