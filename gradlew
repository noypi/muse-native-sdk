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
  *)
    echo "gradlew: unknown task '$TASK'" >&2
    exit 1
    ;;
esac
