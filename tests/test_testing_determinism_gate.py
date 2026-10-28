"""Batch 2 (T4+T5+T6) — determinism runtime gate (FR-TESTING-002).

Covers FR-TESTING-002 / AC-TESTING-015..018 / TC-TESTING-015..018
(``./gradlew :testing:frameStepping``, ``:testing:seedContract``,
``:testing:determinismLint``, ``:testing:determinismReplay``):

- T4: ``class VirtualClock(now_ms: int = 0, vsync_ms: int = 16)`` with
  ``advance_ms(ms: int) -> int``, ``advance_vsyncs(n: int) -> int``,
  ``now() -> int``; ``step_frames(clock, n, on_frame) -> list[int]`` —
  render loop + JS event callback advance from one virtual timeline.
- T5: ``MockHttpServer().enqueue_fixture(path, body)`` /
  ``handle_request(path)`` (``KeyError`` for unenqueued paths, never a
  socket); ``class SeedablePRNG(seed: int)`` with
  ``next_int(bound: int) -> int``; ``pinned_environment(seed)``
  context manager; every randomized-failure message contains
  ``seed=<int>``.
- T6: ``lint_no_wallclock(test_file: str) -> list[str]`` (one
  ``path:line: symbol`` string per forbidden reference); 
  ``replay_twice(seed: int, steps: list[str]) -> dict`` with keys
  ``byte_identical: bool``, ``logs_identical: bool``, ``seed: int``.

Public boundaries under test: ``testing.determinism.clock``,
``testing.determinism.http_mock``, ``testing.determinism.prng``,
``testing.determinism.lint``, ``testing.determinism.replay``, plus the
``:testing:frameStepping`` / ``:testing:seedContract`` /
``:testing:determinismLint`` / ``:testing:determinismReplay`` cases in
``gradlew``.

Hermetic: stdlib unittest only, tmp files, no network/downloads/
device/secrets, no wall-clock or socket reads in the tests.

Expected state before implementation: RED — ``testing/determinism``
does not exist, so every test errors (ModuleNotFoundError) or fails
the file-existence checks.
"""

import os
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DET_DIR = os.path.join(REPO_ROOT, "testing", "determinism")
DET_INIT = os.path.join(DET_DIR, "__init__.py")
CLOCK_PY = os.path.join(DET_DIR, "clock.py")
HTTP_PY = os.path.join(DET_DIR, "http_mock.py")
PRNG_PY = os.path.join(DET_DIR, "prng.py")
LINT_PY = os.path.join(DET_DIR, "lint.py")
REPLAY_PY = os.path.join(DET_DIR, "replay.py")
GRADLEW = os.path.join(REPO_ROOT, "gradlew")

# Plan Batch 2 gradlew tasks (mirrors Batch 1 :testing:cliAllTiers).
EXPECTED_TASKS = (
    ":testing:frameStepping",
    ":testing:seedContract",
    ":testing:determinismLint",
    ":testing:determinismReplay",
)

FORBIDDEN_SYMBOLS = (
    "time.time",
    "monotonic",
    "sleep",
    "socket",
    "urllib",
    "http.client",
    "requests",
)


def read_source(path):
    with open(path, "r", encoding="utf-8") as fh:
        return fh.read()


class DeterminismFilesTest(unittest.TestCase):
    def test_package_init_exists(self):
        self.assertTrue(os.path.isfile(DET_INIT))

    def test_clock_module_exists(self):
        self.assertTrue(os.path.isfile(CLOCK_PY))

    def test_http_mock_module_exists(self):
        self.assertTrue(os.path.isfile(HTTP_PY))

    def test_prng_module_exists(self):
        self.assertTrue(os.path.isfile(PRNG_PY))

    def test_lint_module_exists(self):
        self.assertTrue(os.path.isfile(LINT_PY))

    def test_replay_module_exists(self):
        self.assertTrue(os.path.isfile(REPLAY_PY))

    def test_gradlew_has_frame_stepping_task(self):
        self.assertIn(":testing:frameStepping", read_source(GRADLEW))

    def test_gradlew_has_seed_contract_task(self):
        self.assertIn(":testing:seedContract", read_source(GRADLEW))

    def test_gradlew_has_determinism_lint_task(self):
        self.assertIn(":testing:determinismLint", read_source(GRADLEW))

    def test_gradlew_has_determinism_replay_task(self):
        self.assertIn(":testing:determinismReplay", read_source(GRADLEW))


class VirtualClockTest(unittest.TestCase):
    def test_default_clock_starts_at_zero(self):
        from testing.determinism.clock import VirtualClock
        self.assertEqual(VirtualClock().now(), 0)

    def test_custom_start_time(self):
        from testing.determinism.clock import VirtualClock
        self.assertEqual(VirtualClock(now_ms=1000).now(), 1000)

    def test_advance_ms_returns_new_now(self):
        from testing.determinism.clock import VirtualClock
        clock = VirtualClock()
        self.assertEqual(clock.advance_ms(100), 100)
        self.assertEqual(clock.now(), 100)

    def test_advance_ms_accumulates(self):
        from testing.determinism.clock import VirtualClock
        clock = VirtualClock()
        clock.advance_ms(40)
        clock.advance_ms(60)
        self.assertEqual(clock.now(), 100)

    def test_advance_vsyncs_moves_n_times_vsync(self):
        from testing.determinism.clock import VirtualClock
        clock = VirtualClock(vsync_ms=16)
        self.assertEqual(clock.advance_vsyncs(3), 48)
        self.assertEqual(clock.now(), 48)

    def test_advance_vsyncs_uses_custom_vsync(self):
        from testing.determinism.clock import VirtualClock
        clock = VirtualClock(vsync_ms=11)
        clock.advance_vsyncs(4)
        self.assertEqual(clock.now(), 44)

    def test_advance_vsyncs_zero_is_noop(self):
        from testing.determinism.clock import VirtualClock
        clock = VirtualClock()
        clock.advance_vsyncs(0)
        self.assertEqual(clock.now(), 0)

    def test_advance_negative_ms_raises(self):
        # ASSUMPTION: negative advances are rejected fail-closed.
        from testing.determinism.clock import VirtualClock
        with self.assertRaises(ValueError):
            VirtualClock().advance_ms(-1)

    def test_advance_negative_vsyncs_raises(self):
        # ASSUMPTION: negative advances are rejected fail-closed.
        from testing.determinism.clock import VirtualClock
        with self.assertRaises(ValueError):
            VirtualClock().advance_vsyncs(-1)

    def test_clock_source_reads_no_wallclock(self):
        # AC-TESTING-015: the clock itself must not read wall-clock.
        source = read_source(CLOCK_PY)
        for symbol in ("time.time", "monotonic", "sleep"):
            self.assertNotIn(symbol, source)


class StepFramesTest(unittest.TestCase):
    def test_step_frames_returns_visited_indices(self):
        from testing.determinism.clock import VirtualClock, step_frames
        seen = step_frames(VirtualClock(), 4, lambda frame: None)
        self.assertEqual(seen, [0, 1, 2, 3])

    def test_step_frames_invokes_callback_in_order(self):
        from testing.determinism.clock import VirtualClock, step_frames
        calls = []
        step_frames(VirtualClock(), 5, calls.append)
        self.assertEqual(calls, [0, 1, 2, 3, 4])

    def test_step_frames_zero_calls_nothing(self):
        from testing.determinism.clock import VirtualClock, step_frames
        calls = []
        self.assertEqual(step_frames(VirtualClock(), 0, calls.append), [])
        self.assertEqual(calls, [])

    def test_step_frames_advances_shared_clock(self):
        # AC-TESTING-016: stepping N vsyncs moves now() by N * vsync_ms.
        from testing.determinism.clock import VirtualClock, step_frames
        clock = VirtualClock(vsync_ms=16)
        step_frames(clock, 3, lambda frame: None)
        self.assertEqual(clock.now(), 48)

    def test_render_and_js_observers_share_one_timeline(self):
        # AC-TESTING-016: C++ render-loop and JS event-loop observations
        # interleave on a single ordered log.
        from testing.determinism.clock import VirtualClock, step_frames
        clock = VirtualClock(vsync_ms=16)
        log = []

        def on_frame(frame):
            log.append(("render", clock.now(), frame))
            log.append(("js", clock.now(), frame))

        step_frames(clock, 3, on_frame)
        frames = [entry[2] for entry in log]
        self.assertEqual(frames, [0, 0, 1, 1, 2, 2])
        stamps = [entry[1] for entry in log]
        self.assertEqual(stamps, sorted(stamps))


class MockHttpServerTest(unittest.TestCase):
    def test_enqueue_and_handle_roundtrip(self):
        from testing.determinism.http_mock import MockHttpServer
        server = MockHttpServer()
        server.enqueue_fixture("/items", b'{"n": 1}')
        self.assertEqual(server.handle_request("/items"), b'{"n": 1}')

    def test_unenqueued_path_raises_keyerror(self):
        from testing.determinism.http_mock import MockHttpServer
        with self.assertRaises(KeyError):
            MockHttpServer().handle_request("/missing")

    def test_fixtures_are_independent_per_path(self):
        from testing.determinism.http_mock import MockHttpServer
        server = MockHttpServer()
        server.enqueue_fixture("/a", b"aaa")
        server.enqueue_fixture("/b", b"bbb")
        self.assertEqual(server.handle_request("/a"), b"aaa")
        self.assertEqual(server.handle_request("/b"), b"bbb")

    def test_reenqueue_replaces_fixture(self):
        # ASSUMPTION: re-enqueueing a path replaces the recorded body.
        from testing.determinism.http_mock import MockHttpServer
        server = MockHttpServer()
        server.enqueue_fixture("/x", b"old")
        server.enqueue_fixture("/x", b"new")
        self.assertEqual(server.handle_request("/x"), b"new")

    def test_servers_do_not_share_fixtures(self):
        from testing.determinism.http_mock import MockHttpServer
        first = MockHttpServer()
        first.enqueue_fixture("/only-here", b"data")
        with self.assertRaises(KeyError):
            MockHttpServer().handle_request("/only-here")

    def test_http_mock_source_opens_no_socket(self):
        # AC-TESTING-015: the mock must never open a real socket.
        source = read_source(HTTP_PY)
        for symbol in ("socket", "urllib", "http.client", "requests"):
            self.assertNotIn(symbol, source)


class SeedablePRNGTest(unittest.TestCase):
    def test_same_seed_replays_identical_sequence(self):
        from testing.determinism.prng import SeedablePRNG
        first = [SeedablePRNG(seed=42).next_int(1000) for _ in range(1)]
        run_a = SeedablePRNG(seed=42)
        run_b = SeedablePRNG(seed=42)
        seq_a = [run_a.next_int(1000) for _ in range(20)]
        seq_b = [run_b.next_int(1000) for _ in range(20)]
        self.assertEqual(seq_a, seq_b)
        self.assertEqual(len(first), 1)

    def test_different_seeds_diverge(self):
        from testing.determinism.prng import SeedablePRNG
        run_a = SeedablePRNG(seed=1)
        run_b = SeedablePRNG(seed=2)
        seq_a = [run_a.next_int(2 ** 31) for _ in range(10)]
        seq_b = [run_b.next_int(2 ** 31) for _ in range(10)]
        self.assertNotEqual(seq_a, seq_b)

    def test_next_int_stays_within_bound(self):
        from testing.determinism.prng import SeedablePRNG
        rng = SeedablePRNG(seed=7)
        for _ in range(50):
            value = rng.next_int(10)
            self.assertGreaterEqual(value, 0)
            self.assertLess(value, 10)

    def test_next_int_bound_one_always_zero(self):
        from testing.determinism.prng import SeedablePRNG
        rng = SeedablePRNG(seed=7)
        self.assertEqual(rng.next_int(1), 0)

    def test_next_int_nonpositive_bound_raises(self):
        # ASSUMPTION: non-positive bounds are rejected fail-closed.
        from testing.determinism.prng import SeedablePRNG
        with self.assertRaises(ValueError):
            SeedablePRNG(seed=7).next_int(0)

    def test_failure_template_contains_seed(self):
        # AC-TESTING-017: randomized-failure output prints its seed.
        self.assertIn("seed=", read_source(PRNG_PY))


class PinnedEnvironmentTest(unittest.TestCase):
    def test_pinned_environment_is_context_manager(self):
        from testing.determinism.prng import pinned_environment
        with pinned_environment(seed=123) as env:
            self.assertIsNotNone(env)

    def test_pinned_environment_exposes_fixed_descriptors(self):
        # ASSUMPTION: descriptors cover locale, timezone, fonts, display.
        from testing.determinism.prng import pinned_environment
        with pinned_environment(seed=123) as env:
            for key in ("locale", "timezone", "fonts", "display"):
                self.assertIn(key, env)

    def test_pinned_environment_is_deterministic_per_seed(self):
        from testing.determinism.prng import pinned_environment
        with pinned_environment(seed=9) as first:
            snapshot = dict(first)
        with pinned_environment(seed=9) as second:
            self.assertEqual(dict(second), snapshot)

    def test_pinned_environment_restores_host_env(self):
        from testing.determinism.prng import pinned_environment
        os.environ["MUSE_TEST_PIN_PROBE"] = "before"
        with pinned_environment(seed=9):
            os.environ["MUSE_TEST_PIN_PROBE"] = "inside"
        try:
            self.assertEqual(os.environ.get("MUSE_TEST_PIN_PROBE"), "before")
        finally:
            del os.environ["MUSE_TEST_PIN_PROBE"]


class DeterminismLintTest(unittest.TestCase):
    def _write_case(self, directory, name, body):
        path = os.path.join(directory, name)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(body)
        return path

    def test_clean_file_reports_no_findings(self):
        from testing.determinism.lint import lint_no_wallclock
        with tempfile.TemporaryDirectory() as tmp:
            path = self._write_case(tmp, "clean.py", "VALUE = 1 + 1\n")
            self.assertEqual(lint_no_wallclock(path), [])

    def test_each_forbidden_symbol_is_flagged(self):
        from testing.determinism.lint import lint_no_wallclock
        bodies = {
            "time.time": "import time\nx = time.time()\n",
            "monotonic": "import time\nx = time.monotonic()\n",
            "sleep": "import time\ntime.sleep(1)\n",
            "socket": "import socket\ns = socket.socket()\n",
            "urllib": "import urllib.request\n",
            "http.client": "import http.client\n",
            "requests": "import requests\n",
        }
        with tempfile.TemporaryDirectory() as tmp:
            for symbol, body in bodies.items():
                with self.subTest(symbol=symbol):
                    findings = lint_no_wallclock(
                        self._write_case(tmp, "case.py", body)
                    )
                    self.assertGreaterEqual(len(findings), 1)
                    self.assertTrue(
                        any(symbol in finding for finding in findings)
                    )

    def test_finding_format_is_path_line_symbol(self):
        from testing.determinism.lint import lint_no_wallclock
        with tempfile.TemporaryDirectory() as tmp:
            path = self._write_case(
                tmp, "bad.py", "import socket\ns = socket.socket()\n"
            )
            findings = lint_no_wallclock(path)
            self.assertGreaterEqual(len(findings), 1)
            for finding in findings:
                parts = finding.split(":")
                self.assertGreaterEqual(len(parts), 3)
                self.assertEqual(parts[0], path)
                int(parts[1])  # line number parses as int

    def test_batch1_tier_sources_are_clean(self):
        # AC-TESTING-015: unit/component/golden tier code reads no
        # wall-clock and opens no real socket.
        from testing.determinism.lint import lint_no_wallclock
        tier_dir = os.path.join(REPO_ROOT, "testing", "cli")
        for name in (
            "run.py",
            "cpp_unit.py",
            "js_unit.py",
            "snapshot.py",
            "integration.py",
        ):
            with self.subTest(module=name):
                self.assertEqual(
                    lint_no_wallclock(os.path.join(tier_dir, name)), []
                )

    def test_determinism_sources_are_clean(self):
        from testing.determinism.lint import lint_no_wallclock
        for path in (CLOCK_PY, HTTP_PY, PRNG_PY, LINT_PY, REPLAY_PY):
            with self.subTest(module=os.path.basename(path)):
                self.assertEqual(lint_no_wallclock(path), [])


class DeterminismReplayTest(unittest.TestCase):
    def test_replay_twice_result_shape(self):
        from testing.determinism.replay import replay_twice
        result = replay_twice(seed=42, steps=["a", "b"])
        self.assertIsInstance(result, dict)
        self.assertIn("byte_identical", result)
        self.assertIn("logs_identical", result)
        self.assertIn("seed", result)
        self.assertIsInstance(result["byte_identical"], bool)
        self.assertIsInstance(result["logs_identical"], bool)
        self.assertEqual(result["seed"], 42)

    def test_same_seed_and_timeline_is_identical(self):
        # AC-TESTING-018: same seed + same timeline => byte-identical
        # golden output and identical event-ordering logs.
        from testing.determinism.replay import replay_twice
        result = replay_twice(seed=1234, steps=["boot", "frame", "frame"])
        self.assertTrue(result["byte_identical"])
        self.assertTrue(result["logs_identical"])

    def test_replay_is_stable_across_calls(self):
        from testing.determinism.replay import replay_twice
        steps = ["boot", "scroll", "frame"]
        first = replay_twice(seed=77, steps=steps)
        second = replay_twice(seed=77, steps=list(steps))
        self.assertEqual(first, second)

    def test_empty_steps_replay_identical(self):
        # ASSUMPTION: an empty timeline trivially replays identically.
        from testing.determinism.replay import replay_twice
        result = replay_twice(seed=5, steps=[])
        self.assertTrue(result["byte_identical"])
        self.assertTrue(result["logs_identical"])
        self.assertEqual(result["seed"], 5)


if __name__ == "__main__":
    unittest.main()
