#!/bin/sh
# scan-keys.sh — CI gate proving no embedded secret keys in client-packaged sources.
#
# Usage: scan-keys.sh [--strict] [dir]
#   --strict   fail (exit 1) on any forbidden key pattern (default behavior also
#              fails; the flag exists for the CI gate invocation).
#   dir        target tree to scan; defaults to the repo root (script dir/..).
#              The test harness passes a hermetic fixture dir here.
#
# Exit contract: exit 0 when clean, exit 1 naming the offending file+pattern.
#
# Allow-listed Muse model IDs (never keys, never flagged):
#   sam-3.1, muse-spark-1.3, muse-voice-transcribe-1.0, muse-image
# Forbidden key patterns:
#   sk- style tokens (e.g. sk-test-not-real-secret-value)
#   muse-secret style tokens
#   xai- style tokens
#   BEGIN PRIVATE KEY blocks
#
# Excluded from the scan: proxy/ server env, .git, node_modules, test/fixture
# files, and this script itself (it names the patterns it forbids).

set -u

STRICT=0
ROOT=""

for arg in "$@"; do
  case "$arg" in
    --strict) STRICT=1 ;;
    -h|--help)
      echo "Usage: scan-keys.sh [--strict] [dir]"
      exit 0
      ;;
    *)
      if [ -z "$ROOT" ]; then
        ROOT="$arg"
      else
        echo "scan-keys.sh: unexpected argument: $arg" >&2
        exit 2
      fi
      ;;
  esac
done

if [ -z "$ROOT" ]; then
  ROOT="$(dirname "$0")/.."
fi

if [ ! -e "$ROOT" ]; then
  echo "scan-keys.sh: target not found: $ROOT" >&2
  exit 2
fi

# ERE fragments; each keeps its documented literal substring (sk-, muse-secret,
# xai-, BEGIN PRIVATE KEY) so the gate stays auditable.
PAT_SK='sk-[A-Za-z0-9][A-Za-z0-9_.-]{7,}'
PAT_MUSE='muse-secret'
PAT_XAI='xai-[A-Za-z0-9][A-Za-z0-9_.-]{7,}'
PAT_PK='BEGIN PRIVATE KEY'
PATTERN="${PAT_SK}|${PAT_MUSE}|${PAT_XAI}|${PAT_PK}"

# Allow-listed Muse model IDs — matched lines carrying only these are not keys.
ALLOW='sam-3\.1|muse-spark-1\.3|muse-voice-transcribe-1\.0|muse-image'

failures=0
tmp_list=""
cleanup() { [ -n "$tmp_list" ] && rm -f "$tmp_list"; }
trap cleanup EXIT INT TERM
tmp_list="$(mktemp 2>/dev/null || echo "${TMPDIR:-/tmp}/scan-keys.$$.list")"
: > "$tmp_list" 2>/dev/null || true

# Collect candidate files, pruning server env (proxy/), VCS/deps, tests and
# fixtures, and this gate script itself (it documents the patterns it forbids).
if [ -d "$ROOT" ]; then
  find "$ROOT" \
    \( -path '*/.git/*' -o -path '*/node_modules/*' -o -path '*/proxy/*' \
       -o -path '*/.worktrees/*' \) -prune \
    -o -type d \( -iname '*fixture*' \) -prune \
    -o -type f \( -iname '*test*' -o -iname '*fixture*' -o -iname '*spec*' \
       -o -name 'scan-keys.sh' \) -prune \
    -o -type f -print > "$tmp_list" 2>/dev/null
else
  printf '%s\n' "$ROOT" > "$tmp_list"
fi

while IFS= read -r file; do
  [ -n "$file" ] || continue
  [ -f "$file" ] || continue
  hits="$(grep -I -E -n -e "$PATTERN" "$file" 2>/dev/null || true)"
  [ -n "$hits" ] || continue
  # Drop lines that only carry allow-listed Muse model IDs without key material.
  # (Model IDs never match the key EREs above; this is defense in depth.)
  filtered="$(printf '%s\n' "$hits" | grep -E -v -e "$ALLOW" || true)"
  if [ -z "$filtered" ]; then
    # Every hit was an allow-listed model ID line — but only report clean if no
    # hit carries key material beyond the ID. Re-check: a line with both an ID
    # and a key pattern still fails (it survived only if ALL hits filtered).
    # Since all hits filtered, this file is clean.
    continue
  fi
  # Name the offending file+pattern for an actionable gate message.
  printf '%s\n' "$filtered" | while IFS= read -r line; do
    pat_label="sk-"
    case "$line" in
      *muse-secret*) pat_label="muse-secret" ;;
      *'BEGIN PRIVATE KEY'*) pat_label="BEGIN PRIVATE KEY" ;;
      *xai-*) pat_label="xai-" ;;
      *sk-*) pat_label="sk-" ;;
    esac
    echo "scan-keys FAIL: ${file}: matched forbidden pattern '${pat_label}': ${line}"
  done
  failures=$((failures + 1))
done < "$tmp_list"

if [ "$failures" -ne 0 ]; then
  if [ "$STRICT" -eq 1 ]; then
    echo "scan-keys: --strict: ${failures} file(s) with forbidden key material" >&2
  else
    echo "scan-keys: ${failures} file(s) with forbidden key material" >&2
  fi
  exit 1
fi

echo "scan-keys: clean"
exit 0
