"""Batch 1 (T3) — Skia notices entry in third-party-notices.txt.

Covers FR-DEPS-011 / AC-DEPS-029 per specs-cpp-dependencies.md: the
snapshot carries the BSD-3-Clause Google Inc. text and the generated
third-party notices file lists it; a missing licence text or absent
notices entry fails admission (TC-DEPS-029).

Hermetic: stdlib unittest only (text reads, tmp files for the negative
cases). No downloads, no secrets, no services.
Expected state before implementation: RED — the committed notices file
has no Skia entry, so the entry tests fail; only the helper negative
cases pass.
"""

import os
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NOTICES = os.path.join(
    REPO_ROOT, "app", "src", "main", "assets", "third-party-notices.txt"
)

SKIA_NAME = "Skia Graphite"
SKIA_LICENCE = "BSD-3-Clause"
SKIA_HOLDER = "Google Inc."


def read_notices(path=NOTICES):
    """Read a notices file as text."""
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


def notices_gaps(text, name=SKIA_NAME):
    """Field names absent from ``text`` for the named entry."""
    gaps = []
    if name not in text:
        return ["missing-entry"]
    # Licence/holder must appear at or after the entry heading.
    tail = text.split(name, 1)[1]
    if SKIA_LICENCE not in tail:
        gaps.append("licence")
    if SKIA_HOLDER not in tail:
        gaps.append("holder")
    return gaps


class TestSkiaNoticesEntry(unittest.TestCase):
    """AC-DEPS-029: committed notices file lists Skia with licence text."""

    def test_notices_file_exists(self):
        """Notices file is present and non-empty."""
        self.assertTrue(os.path.isfile(NOTICES))
        self.assertTrue(read_notices())

    def test_skia_entry_present(self):
        """Notices file lists Skia Graphite (TC-DEPS-029)."""
        self.assertIn(
            SKIA_NAME, read_notices(),
            "notices file missing Skia Graphite entry (AC-DEPS-029)",
        )

    def test_skia_licence_text_present(self):
        """Entry carries the BSD-3-Clause licence text."""
        self.assertEqual(
            notices_gaps(read_notices()), [],
            "notices entry gaps: %r (AC-DEPS-029)"
            % (notices_gaps(read_notices()),),
        )


class TestSkiaNoticesNegatives(unittest.TestCase):
    """AC-DEPS-029: absent entry/licence text is detectable (fail closed)."""

    def _write(self, text):
        tmp = tempfile.NamedTemporaryFile(
            mode="w", suffix=".txt", delete=False, encoding="utf-8"
        )
        tmp.write(text)
        tmp.close()
        return tmp.name

    def test_absent_entry_flagged(self):
        """Notices without any Skia entry report a missing entry."""
        path = self._write("Third-Party Notices\nHermes — Licence: MIT\n")
        try:
            self.assertEqual(
                notices_gaps(read_notices(path)), ["missing-entry"]
            )
        finally:
            os.unlink(path)

    def test_entry_without_licence_flagged(self):
        """Entry naming Skia but no BSD-3-Clause text is a gap (boundary)."""
        path = self._write(
            "Third-Party Notices\n%s — Licence: MIT — %s\n"
            % (SKIA_NAME, SKIA_HOLDER)
        )
        try:
            self.assertIn("licence", notices_gaps(read_notices(path)))
        finally:
            os.unlink(path)

    def test_entry_without_holder_flagged(self):
        """Entry without the Google Inc. holder text is a gap (boundary)."""
        path = self._write(
            "Third-Party Notices\n%s — Licence: %s\n"
            % (SKIA_NAME, SKIA_LICENCE)
        )
        try:
            self.assertIn("holder", notices_gaps(read_notices(path)))
        finally:
            os.unlink(path)

    def test_wrong_holder_flagged(self):
        """A non-Google holder does not satisfy the entry (boundary)."""
        path = self._write(
            "Third-Party Notices\n%s — Licence: %s — Someone Else\n"
            % (SKIA_NAME, SKIA_LICENCE)
        )
        try:
            self.assertIn("holder", notices_gaps(read_notices(path)))
        finally:
            os.unlink(path)


if __name__ == "__main__":
    unittest.main()
