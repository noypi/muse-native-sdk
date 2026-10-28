"""Golden harness package (FR-TESTING-004).

Hermetic stdlib-only golden/screenshot harness; consumes
``engine/layout/m3_matrix.json`` and the frozen font manifest.
"""

from testing.goldens.coverage import golden_coverage, render_golden

__all__ = ["golden_coverage", "render_golden"]
