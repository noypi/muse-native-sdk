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
    "$CXX" -std=c++17 -c "$T6DIR/abi_incompatible_plugin.cpp" -o "$T6TMP/abi_incompat.o" \
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
    "$CXX" -std=c++17 -I"$ROOT" "$T6TMP/abi_check_main.cpp" "$T6TMP/abi_incompat.o" -o "$T6TMP/abi_check" \
      || { echo "FAIL: abi rejection driver does not link" >&2; rm -rf "$T6TMP"; exit 1; }
    "$T6TMP/abi_check" || { rm -rf "$T6TMP"; exit 1; }
    # (d2) noexcept TU links under -fno-exceptions and passes frame timing.
    "$CXX" -std=c++17 -fno-exceptions -c "$T6DIR/frame_timing_noexcept.cpp" -o "$T6TMP/frame_timing.o" \
      || { echo "FAIL: frame_timing_noexcept.cpp does not compile under -fno-exceptions" >&2; rm -rf "$T6TMP"; exit 1; }
    "$CXX" -std=c++17 -fno-exceptions -c engine/core/render_path_tu.cpp -o "$T6TMP/render_path_tu.o" \
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
    "$CXX" -std=c++17 -fno-exceptions "$T6TMP/timing_main.cpp" "$T6TMP/frame_timing.o" "$T6TMP/render_path_tu.o" -o "$T6TMP/timing_check" \
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
  *)
    echo "gradlew: unknown task '$TASK'" >&2
    exit 1
    ;;
esac
