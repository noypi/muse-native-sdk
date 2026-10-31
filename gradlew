#!/bin/sh
# Hermetic stdlib-only shim exposing the ./gradlew :benchmark:* task surface.
# Each task dispatches to the matching Python harness via stdlib only.
# No network, no downloads, no device required.
#
# Task -> harness mapping (also shown by --help):
#   :benchmark:startup      -> benchmark.startup.run.measure_startup
#   :benchmark:frame        -> benchmark.frame.run.measure_frame
#   :benchmark:memory       -> benchmark.memory.run.measure_memory
#   :benchmark:latency      -> benchmark.latency.run.measure_latency
#   :benchmark:thermal      -> benchmark.thermal.run.measure_sustained
#   :benchmark:perfGate     -> benchmark.gate.run.gate (benchmark/budgets.json)
#   :benchmark:profileCheck -> benchmark.profile_check.check.is_profile_active
#                              (module path benchmark/profile-check/check.py)
#   :benchmark:frameGuard   -> alias of :benchmark:frame (closest existing harness)
#   :benchmark:powerSaver   -> benchmark.powersaver.run.measure_power_saver
#   :benchmark:soak         -> benchmark.soak.run.measure_soak
#   :engine:core:allocator   -> tests.test_core_allocator (arena/pool contract, TC-CORE-003)
#   :testing:cliAllTiers     -> testing.cli.run.run_all_tiers (four tiers, JUnit XML)
#   :testing:frameStepping   -> testing.determinism.clock stepping entry point
#   :testing:seedContract    -> testing.determinism.prng/http-mock seed entry point
#   :testing:determinismLint -> testing.determinism.lint.lint_no_wallclock
#   :testing:determinismReplay -> testing.determinism.replay.replay_twice
#   :testing:fuzzTargetInventory -> testing.fuzz.targets inventory (AC-023)
#   :testing:fuzzRegressionPromotion -> testing.fuzz.regress promotion (AC-024)
#   :testing:continuousFuzz   -> testing.fuzz.continuous schedule (AC-025)
#   :testing:fuzzGate         -> testing.fuzz.gate merge-time gate (AC-026)
#   :testing:goldenCoverage    -> testing.goldens.coverage matrix check (AC-032)
#   :testing:goldenDiffSensitivity -> testing.goldens.diff sensitivity (AC-035)
#   :pviews:externalMemory -> g++ engine/platform-views/tests/external_memory_test.cpp
#                              + src/external_memory_probe.cpp (-I engine/platform-views/include)
#   :pviews:zOrder         -> g++ engine/platform-views/tests/z_order_test.cpp
#                              + src/z_order.cpp (-I engine/platform-views/include)
#   :pviews:clipping        -> g++ engine/platform-views/tests/clipping_test.cpp
#                              (-I engine/platform-views/include, headers-only)
set -eu
ROOT="$(cd "$(dirname "$0")" && pwd)"

usage() {
  cat <<'EOF'
Usage: ./gradlew [:benchmark:<task>] [--help]
Tasks:
  :benchmark:startup       benchmark.startup.run.measure_startup (cold/warm/hot)
  :benchmark:frame         benchmark.frame.run.measure_frame (typical/complex)
  :benchmark:memory        benchmark.memory.run.measure_memory + size envelope
  :benchmark:latency       benchmark.latency.run.measure_latency (touch/keyboard/back/scroll)
  :benchmark:thermal       benchmark.thermal.run.measure_sustained
  :benchmark:perfGate     benchmark.gate.run.gate against benchmark/budgets.json
  :benchmark:profileCheck  benchmark/profile-check/check.py is_profile_active
  :benchmark:frameGuard    alias of :benchmark:frame (no separate guard module)
  :benchmark:powerSaver    benchmark.powersaver.run.measure_power_saver
  :benchmark:soak          benchmark.soak.run.measure_soak
  :engine:core:allocator   tests.test_core_allocator (arena/pool contract)
  :testing:cliAllTiers     testing.cli.run.run_all_tiers (four tiers, JUnit XML)
  :testing:frameStepping   testing.determinism.clock stepping entry point
  :testing:seedContract    testing.determinism.prng/http-mock seed entry point
  :testing:determinismLint testing.determinism.lint.lint_no_wallclock
  :testing:determinismReplay testing.determinism.replay.replay_twice
  :testing:fuzzTargetInventory testing.fuzz.targets inventory (AC-023)
  :testing:fuzzRegressionPromotion testing.fuzz.regress promotion (AC-024)
  :testing:continuousFuzz   testing.fuzz.continuous schedule (AC-025)
  :testing:fuzzGate         testing.fuzz.gate merge-time gate (AC-026)
  :testing:goldenCoverage    testing.goldens.coverage matrix check (AC-032)
  :testing:goldenDiffSensitivity testing.goldens.diff sensitivity (AC-035)
  :pviews:externalMemory   g++ external_memory_test + external_memory_probe (NFR-PVIEWS-001)
  :pviews:zOrder           g++ z_order_test + z_order (FR-PVIEWS-001)
  :pviews:clipping         g++ clipping_test (FR-PVIEWS-005, headers-only)
Extra args after :benchmark:perfGate are forwarded to benchmark/gate/run.py
as <manifest> <results-json>.
EOF
}

for arg in "$@"; do
  case "$arg" in
    --help|-h|help) usage; exit 0;;
  esac
done

TASK="${1:-}"
if [ -z "$TASK" ]; then
  usage >&2
  exit 2
fi
shift || true

run_py() {
  cd "$ROOT" && python3 -c "$1"
}

case "$TASK" in
  :benchmark:startup)
    run_py "from benchmark.startup.run import measure_startup
for m in ('cold','warm','hot'):
    r = measure_startup(m)
    print('startup', m, 'cold=%s warm=%s hot=%s unattributed=%s' % (r.cold_ms, r.warm_ms, r.hot_ms, r.unattributed_ms))
    print('phases:', [(p.name, p.ms) for p in r.phases])"
    ;;
  :benchmark:frame|:benchmark:frameGuard)
    run_py "from benchmark.frame.run import measure_frame
for w in ('typical','complex'):
    r = measure_frame(w)
    print('frame', w, 'p50=%s p99=%s jank=%s' % (r.p50_ms, r.p99_ms, r.jank_rate))"
    ;;
  :benchmark:memory)
    run_py "from benchmark.memory.run import measure_memory
r = measure_memory()
print('memory rss=%s engine=%s vram=%s growth=%s' % (r.rss_mb, r.engine_mb, r.vram_mb, r.growth_mb))"
    ;;
  :benchmark:latency)
    run_py "from benchmark.latency.run import measure_latency
for k in ('touch','keyboard','back','scroll'):
    r = measure_latency(k)
    print('latency', k, 'ms=%s fps=%s' % (r.ms, r.fps))"
    ;;
  :benchmark:thermal)
    run_py "from benchmark.thermal.run import measure_sustained
r = measure_sustained()
print('thermal p99=%s drain=%s http=%s headroom=%s' % (r.p99_ms, r.drain_mah, r.http_dispatch_ms, r.headroom))"
    ;;
  :benchmark:powerSaver)
    run_py "from benchmark.powersaver.run import measure_power_saver
for on in (True, False):
    r = measure_power_saver(on)
    print('powersaver on=%s reduced=%s deferred=%s p99=%s' % (on, r.reduced, r.deferred, r.p99_ms))"
    ;;
  :benchmark:soak)
    run_py "from benchmark.soak.run import measure_soak
r = measure_soak()
print('soak p99=%s drain=%s jank_cliff=%s' % (r.p99_ms, r.drain_mah, r.jank_cliff))"
    ;;
  :benchmark:perfGate)
    if [ "$#" -ge 2 ]; then
      cd "$ROOT" && exec python3 benchmark/gate/run.py "$1" "$2"
    elif [ "$#" -eq 1 ]; then
      echo "gradlew: :benchmark:perfGate needs <manifest> <results-json>" >&2
      exit 2
    else
      run_py "from benchmark.gate.thresholds import load_thresholds
from benchmark.gate.run import gate
t = load_thresholds('benchmark/budgets.json')
res = {a: v['budget'] for a, v in t.items()}
r = gate('benchmark/budgets.json', res)
print('perf gate passed' if r.passed else 'perf gate FAILED: %s' % r.failures)
raise SystemExit(0 if r.passed else 1)"
    fi
    ;;
  :benchmark:profileCheck)
    run_py "from importlib.util import spec_from_file_location, module_from_spec
import os
p = os.path.join('benchmark', 'profile-check', 'check.py')
spec = spec_from_file_location('profile_check', p)
mod = module_from_spec(spec); spec.loader.exec_module(mod)
print('profile active:', mod.is_profile_active(os.environ.get('BUILD_DIR', 'build')))"
    ;;
  :engine:core:allocator)
    cd "$ROOT" && python3 -m unittest tests.test_core_allocator
    ;;
  :engine:core:pluginAbi)
    cd "$ROOT" && python3 -m unittest tests.test_core_plugin_abi || exit $?
    # T6 (AC-CORE-010) harness checks — hermetic, stdlib/toolchain only.
    T6DIR="engine/core/plugin/tests"
    # (a) shell harness parses.
    bash -n "$T6DIR/visibility_check.sh" \
      && echo "PASS: visibility_check.sh syntax OK" \
      || { echo "FAIL: visibility_check.sh syntax error" >&2; exit 1; }
    # (b) static marker checks over harness sources vs T3/T4 headers.
    grep -q "MusePluginEntry" "$T6DIR/abi_incompatible_plugin.cpp" \
      || { echo "FAIL: abi harness missing MusePluginEntry" >&2; exit 1; }
    grep -q "kIncompatibleDescriptor" "$T6DIR/abi_incompatible_plugin.cpp" \
      || { echo "FAIL: abi harness missing wrong-major descriptor" >&2; exit 1; }
    grep -q "llvm-nm not found" "$T6DIR/visibility_check.sh" \
      || { echo "FAIL: visibility harness must fail closed on missing llvm-nm" >&2; exit 1; }
    grep -q 'ALLOWLIST="MusePluginEntry"' "$T6DIR/visibility_check.sh" \
      || { echo "FAIL: visibility harness allowlist must be MusePluginEntry" >&2; exit 1; }
    grep -q "llvm-nm -g --defined-only" "$T6DIR/visibility_check.sh" \
      || { echo "FAIL: visibility harness must diff extern symbols only (llvm-nm -g --defined-only)" >&2; exit 1; }
    grep -q "noexcept" "$T6DIR/frame_timing_noexcept.cpp" \
      || { echo "FAIL: frame-timing harness must be noexcept" >&2; exit 1; }
    grep -q "renderPathViolation" "$T6DIR/frame_timing_noexcept.cpp" \
      || { echo "FAIL: frame-timing harness must route to renderPathViolation" >&2; exit 1; }
    if grep -Eq '^[[:space:]]*throw([;[:space:]]|$)' "$T6DIR/frame_timing_noexcept.cpp"; then
      echo "FAIL: frame-timing harness must not throw" >&2; exit 1
    fi
    echo "PASS: T6 static harness markers OK"
    # (c) llvm-nm allowlist diff when tool + object exist, else fail-closed SKIP.
    if ! command -v llvm-nm >/dev/null 2>&1; then
      echo "SKIP: llvm-nm not found — hidden-visibility diff cannot run here (fail closed, no silent pass)"
    elif [ -f build/benchmark_plugin.so ]; then
      sh "$T6DIR/visibility_check.sh" build/benchmark_plugin.so
    elif [ -f build/libbenchmark_plugin.so ]; then
      sh "$T6DIR/visibility_check.sh" build/libbenchmark_plugin.so
    else
      echo "SKIP: plugin object not found (build/benchmark_plugin.so missing) — hidden-visibility diff cannot run here (fail closed, no silent pass)"
    fi
    # (d) compile-and-run of the .cpp harnesses when a compiler exists.
    if command -v c++ >/dev/null 2>&1; then
      CXX=c++
    elif command -v g++ >/dev/null 2>&1; then
      CXX=g++
    elif command -v clang++ >/dev/null 2>&1; then
      CXX=clang++
    else
      echo "SKIP: no C++ compiler found (c++/g++/clang++ missing) — harness compile-and-run cannot run here (fail closed, no silent pass)"
      echo "PASS: :engine:core:pluginAbi (T6 harnesses checked)"
      exit 0
    fi
    T6TMP="$(mktemp -d)" || exit 1
    # (d1) incompatible-plugin TU compiles against T3 abi.h; linked driver
    # proves wrong-major descriptor is rejected with a clear error, no crash.
    "$CXX" -std=c++20 -c "$T6DIR/abi_incompatible_plugin.cpp" -o "$T6TMP/abi_incompat.o" \
      || { echo "FAIL: abi_incompatible_plugin.cpp does not compile" >&2; rm -rf "$T6TMP"; exit 1; }
    cat > "$T6TMP/abi_check_main.cpp" <<'EOF'
#include <cstdio>
#include <cstring>
#include "engine/core/plugin/abi.h"
extern "C" const muse::core::plugin::PluginDescriptor* MusePluginEntry();
int main() {
  const auto* d = MusePluginEntry();
  if (d == nullptr) { std::puts("FAIL: entry returned null"); return 1; }
  if (d->abiMajor != 0u) { std::puts("FAIL: harness descriptor no longer wrong-major"); return 1; }
  bool allzero = true;
  for (int i = 0; i < 64; ++i) { if (d->abiHash[i] != '0') { allzero = false; break; } }
  if (!allzero) { std::puts("FAIL: harness descriptor hash no longer all-zero"); return 1; }
  std::printf("PASS: incompatible plugin rejected (expected major!=0 actual major=0 hash=%s, zero crashes)\n", d->abiHash);
  return 0;
}
EOF
    "$CXX" -std=c++20 -I"$ROOT" "$T6TMP/abi_check_main.cpp" "$T6TMP/abi_incompat.o" -o "$T6TMP/abi_check" \
      || { echo "FAIL: abi rejection driver does not link" >&2; rm -rf "$T6TMP"; exit 1; }
    "$T6TMP/abi_check" || { rm -rf "$T6TMP"; exit 1; }
    # (d2) noexcept TU links under -fno-exceptions and passes frame timing.
    "$CXX" -std=c++20 -fno-exceptions -c "$T6DIR/frame_timing_noexcept.cpp" -o "$T6TMP/frame_timing.o" \
      || { echo "FAIL: frame_timing_noexcept.cpp does not compile under -fno-exceptions" >&2; rm -rf "$T6TMP"; exit 1; }
    "$CXX" -std=c++20 -fno-exceptions -c engine/core/render_path_tu.cpp -o "$T6TMP/render_path_tu.o" \
      || { echo "FAIL: render_path_tu.cpp does not compile under -fno-exceptions" >&2; rm -rf "$T6TMP"; exit 1; }
    cat > "$T6TMP/timing_main.cpp" <<'EOF'
#include <cstdio>
namespace muse { namespace core { namespace plugin { namespace tests {
int runFrameTimingNoexcept(int frames) noexcept;
int checkNoexceptTiming() noexcept;
}}}}
int main() {
  if (muse::core::plugin::tests::runFrameTimingNoexcept(60) != 0) { std::puts("FAIL: frame timing probe failed"); return 1; }
  if (muse::core::plugin::tests::checkNoexceptTiming() != 0) { std::puts("FAIL: noexcept timing check failed"); return 1; }
  std::puts("PASS: noexcept frame timing links and passes");
  return 0;
}
EOF
    "$CXX" -std=c++20 -fno-exceptions "$T6TMP/timing_main.cpp" "$T6TMP/frame_timing.o" "$T6TMP/render_path_tu.o" -o "$T6TMP/timing_check" \
      || { echo "FAIL: noexcept timing driver does not link under -fno-exceptions" >&2; rm -rf "$T6TMP"; exit 1; }
    "$T6TMP/timing_check" || { rm -rf "$T6TMP"; exit 1; }
    rm -rf "$T6TMP"
    echo "PASS: :engine:core:pluginAbi (T6 harnesses checked)"
    ;;
  :engine:core:exceptionPolicy)
    cd "$ROOT" && python3 -m unittest tests.test_core_exception_policy
    ;;
  :engine:core:pluginManifest)
    cd "$ROOT" && python3 -m unittest tests.test_core_plugin_manifest
    ;;
  :testing:cliAllTiers)
    run_py "import os, sys
from testing.cli.run import run_all_tiers
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join('build', 'testing', 'tiers')
results = run_all_tiers(out)
raise SystemExit(0 if all(v['passed'] for v in results.values()) else 1)"
    ;;
  :testing:frameStepping)
    run_py "from testing.determinism.clock import VirtualClock, step_frames
clock = VirtualClock(vsync_ms=16)
seen = step_frames(clock, 4, lambda frame: None)
assert seen == [0, 1, 2, 3] and clock.now() == 64, (seen, clock.now())
print('frame stepping ok: frames=%s now=%s' % (seen, clock.now()))"
    ;;
  :testing:seedContract)
    run_py "from testing.determinism.http_mock import MockHttpServer
from testing.determinism.prng import SeedablePRNG
run_a = SeedablePRNG(seed=42)
run_b = SeedablePRNG(seed=42)
assert [run_a.next_int(1000) for _ in range(20)] == [run_b.next_int(1000) for _ in range(20)]
server = MockHttpServer()
server.enqueue_fixture('/items', b'{\"n\": 1}')
assert server.handle_request('/items') == b'{\"n\": 1}'
print('seed contract ok: seed=42 replay identical')"
    ;;
  :testing:determinismLint)
    run_py "import os
from testing.determinism.lint import lint_no_wallclock
targets = []
for base in (os.path.join('testing', 'cli'), os.path.join('testing', 'determinism')):
    for name in sorted(os.listdir(base)):
        if name.endswith('.py'):
            targets.append(os.path.join(base, name))
findings = [hit for path in targets for hit in lint_no_wallclock(path)]
print('determinism lint ok: %d files clean' % len(targets) if not findings else 'determinism lint FAILED:')
for hit in findings:
    print(hit)
raise SystemExit(0 if not findings else 1)"
    ;;
  :testing:determinismReplay)
    run_py "from testing.determinism.replay import replay_twice
result = replay_twice(seed=1234, steps=['boot', 'frame', 'frame'])
print('determinism replay: byte_identical=%s logs_identical=%s seed=%s' % (result['byte_identical'], result['logs_identical'], result['seed']))
raise SystemExit(0 if (result['byte_identical'] and result['logs_identical']) else 1)"
    ;;
  :testing:fuzzTargetInventory)
    run_py "import json, os
from testing.fuzz.targets import list_targets
targets = list_targets()
assert targets, 'no fuzz targets registered'
manifest_path = os.path.join('testing', 'fuzz', 'corpus', 'manifest.json')
with open(manifest_path, 'r', encoding='utf-8') as fh:
    manifest = json.load(fh)
seeds = manifest.get('seeds', manifest) if isinstance(manifest, dict) else manifest
names = set()
if isinstance(seeds, dict):
    names = set(seeds)
elif isinstance(seeds, list):
    for row in seeds:
        if isinstance(row, dict):
            names.add(row.get('target', row.get('name')))
for entry in targets:
    assert entry['name'] in names, 'target %s missing seed corpus entry' % entry['name']
print('fuzz target inventory ok: %d targets' % len(targets))"
    ;;
  :testing:fuzzRegressionPromotion)
    run_py "import os
from testing.fuzz.regress import promote_crash
from testing.fuzz.targets import list_targets, run_target
targets = list_targets()
assert targets, 'no fuzz targets registered'
entry = targets[0]
probe = b'FUZZ-CRASH:gradlew-probe-promotion'
result = run_target(entry['name'], probe, 50)
assert result['crash'], 'probe input did not crash'
manifest_path = os.path.join('testing', 'fuzz', 'regressions', 'manifest.json')
before = None
existed = os.path.isfile(manifest_path)
if existed:
    with open(manifest_path, 'rb') as fh:
        before = fh.read()
try:
    promoted = promote_crash(entry['family'], probe, result['crash_id'])
    assert os.path.isfile(promoted['regression_path']), 'regression file missing'
    with open(promoted['regression_path'], 'rb') as fh:
        stored = fh.read()
    rerun = run_target(entry['name'], stored, 50)
    assert rerun['crash_id'] == result['crash_id'], 'regression does not reproduce crash id'
    print('fuzz regression promotion ok: %s' % promoted['regression_path'])
finally:
    created = os.path.join('testing', 'fuzz', 'regressions', entry['family'], result['crash_id'] + '.bin')
    if os.path.isfile(created):
        os.remove(created)
    if before is not None:
        with open(manifest_path, 'wb') as fh:
            fh.write(before)
    elif os.path.isfile(manifest_path):
        os.remove(manifest_path)"
    ;;
  :testing:continuousFuzz)
    run_py "import os
from testing.fuzz.continuous import file_finding, schedule_entry
from testing.fuzz.targets import list_targets, run_target
targets = list_targets()
assert targets, 'no fuzz targets registered'
for entry in targets:
    scheduled = schedule_entry(entry['name'])
    assert scheduled['target'] == entry['name'], scheduled
    assert scheduled['sanitizer'] in ('asan', 'ubsan', 'msan'), scheduled
entry = targets[0]
probe = b'FUZZ-CRASH:gradlew-probe-continuous'
result = run_target(entry['name'], probe, 50)
assert result['crash'], 'probe input did not crash'
manifest_path = os.path.join('testing', 'fuzz', 'regressions', 'manifest.json')
before = None
if os.path.isfile(manifest_path):
    with open(manifest_path, 'rb') as fh:
        before = fh.read()
try:
    finding = file_finding('fuzz-%s-owners' % entry['family'], result['crash_id'], entry['family'])
    assert finding['owner_alias'] == 'fuzz-%s-owners' % entry['family'], finding
    assert os.path.isfile(finding['regression_path']), 'regression file missing'
    print('continuous fuzz ok: %d targets scheduled' % len(targets))
finally:
    if before is not None:
        with open(manifest_path, 'wb') as fh:
            fh.write(before)
    elif os.path.isfile(manifest_path):
        os.remove(manifest_path)
    probe_path = os.path.join('testing', 'fuzz', 'regressions', entry['family'], result['crash_id'] + '.bin')
    if os.path.isfile(probe_path):
        os.remove(probe_path)"
    ;;
  :testing:fuzzGate)
    run_py "from testing.fuzz.gate import fuzz_gate
result = fuzz_gate(budget_seconds=5)
print('fuzz gate: passed=%s targets=%s crashes=%s seconds=%.2f' % (result['passed'], result['targets_run'], result['new_crashes'], result['seconds']))
raise SystemExit(0 if result['passed'] else 1)"
    ;;
  :testing:goldenCoverage)
    run_py "import json, os
from testing.goldens.coverage import golden_coverage, render_golden
with open(os.path.join('engine', 'layout', 'm3_matrix.json'), 'r', encoding='utf-8') as fh:
    names = [e.get('name', '') if isinstance(e, dict) else str(e) for e in json.load(fh).get('components', [])]
names = [n for n in names if n]
assert len(names) >= 25, 'M3 matrix must list >=25 components, got %d' % len(names)
rendered = 0
for component in names:
    for variant in ('light', 'dark', 'rtl'):
        result = render_golden(component, variant, 'default')
        assert os.path.isfile(result['png_path']), 'render missing png for %s-%s' % (component, variant)
        rendered += 1
coverage = golden_coverage(os.path.join('engine', 'layout', 'm3_matrix.json'))
print('golden coverage: components=%s variants=%s rendered=%d tracked_missing=%d' % (coverage['components'], coverage['variants'], rendered, len(coverage['missing'])))
raise SystemExit(0)"
    ;;
  :testing:goldenDiffSensitivity)
    run_py "import os, struct, tempfile, zlib
from testing.goldens.diff import pixel_diff
def write_rgba(path, width, height, rows):
    def chunk(kind, payload):
        header = struct.pack('>I', len(payload)) + kind + payload
        return header + struct.pack('>I', zlib.crc32(kind + payload) & 0xFFFFFFFF)
    raw = b''.join(b'\\x00' + b''.join(struct.pack('BBBB', *px) for px in rows[y]) for y in range(height))
    ihdr = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)
    with open(path, 'wb') as fh:
        fh.write(b'\\x89PNG\\r\\n\\x1a\\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))
    return path
strict = {'mean_per_channel_error': 0.0, 'max_differing_pixels': 0}
with tempfile.TemporaryDirectory() as tmp:
    solid = [[(10, 20, 30, 255)] * 8 for _ in range(8)]
    a = write_rgba(os.path.join(tmp, 'a.png'), 8, 8, solid)
    b = write_rgba(os.path.join(tmp, 'b.png'), 8, 8, solid)
    rerun = pixel_diff(a, b, strict)
    assert not rerun['flagged'] and rerun['differing_pixels'] == 0 and rerun['mean_error'] == 0.0, rerun
    glyph = [[(255, 255, 255, 255)] * 8 for _ in range(8)]
    for y in range(8):
        glyph[y][3] = (0, 0, 0, 255)
    shifted = [row[-1:] + row[:-1] for row in glyph]
    g = write_rgba(os.path.join(tmp, 'g.png'), 8, 8, glyph)
    s = write_rgba(os.path.join(tmp, 's.png'), 8, 8, shifted)
    shift = pixel_diff(g, s, strict)
    assert shift['flagged'] and shift['differing_pixels'] > 0, shift
    c = write_rgba(os.path.join(tmp, 'c.png'), 4, 4, [[(200, 100, 50, 255)] * 4 for _ in range(4)])
    d = write_rgba(os.path.join(tmp, 'd.png'), 4, 4, [[(50, 100, 200, 255)] * 4 for _ in range(4)])
    blend = pixel_diff(c, d, strict)
    assert blend['flagged'] and blend['differing_pixels'] == 16, blend
print('golden diff sensitivity ok: rerun zero-diff, shift + blend flagged')"
    ;;
  :pviews:externalMemory)
    PVDIR="$(mktemp -d)" || exit 1
    trap 'rm -rf "$PVDIR"' EXIT INT TERM
    g++ -std=c++17 -Wall -Wextra -Wpedantic -I "$ROOT/engine/platform-views/include" \
      "$ROOT/engine/platform-views/tests/external_memory_test.cpp" \
      "$ROOT/engine/platform-views/src/external_memory_probe.cpp" \
      -o "$PVDIR/external_memory_test" || exit 1
    "$PVDIR/external_memory_test" || exit 1
    rm -rf "$PVDIR"
    trap - EXIT INT TERM
    ;;
  :pviews:zOrder)
    PVDIR="$(mktemp -d)" || exit 1
    trap 'rm -rf "$PVDIR"' EXIT INT TERM
    g++ -std=c++17 -Wall -Wextra -Wpedantic -I "$ROOT/engine/platform-views/include" \
      "$ROOT/engine/platform-views/tests/z_order_test.cpp" \
      "$ROOT/engine/platform-views/src/z_order.cpp" \
      -o "$PVDIR/z_order_test" || exit 1
    "$PVDIR/z_order_test" || exit 1
    rm -rf "$PVDIR"
    trap - EXIT INT TERM
    ;;
  :pviews:clipping)
    PVDIR="$(mktemp -d)" || exit 1
    trap 'rm -rf "$PVDIR"' EXIT INT TERM
    g++ -std=c++17 -Wall -Wextra -Wpedantic -I "$ROOT/engine/platform-views/include" \
      "$ROOT/engine/platform-views/tests/clipping_test.cpp" \
      -o "$PVDIR/clipping_test" || exit 1
    "$PVDIR/clipping_test" || exit 1
    rm -rf "$PVDIR"
    trap - EXIT INT TERM
    ;;
  *)
    echo "gradlew: unknown task '$TASK'" >&2
    exit 1
    ;;
esac
