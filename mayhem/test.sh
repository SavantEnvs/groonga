#!/usr/bin/env bash
#
# mayhem/test.sh — replays curated upstream command-suite cases through the pre-built normal-flags
# `groonga` executable (mayhem/build-test) and checks every response against the case's committed
# .expected file. Never compiles. exit 0 = pass; reports a CTRF summary.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SRC:=/mayhem}"
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

GROONGA="$SRC/mayhem/build-test/src/groonga"
if [ ! -x "$GROONGA" ]; then
  echo "missing $GROONGA — mayhem/build.sh must build it" >&2
  emit_ctrf "groonga-command-suite" 0 1 0
  exit 1
fi
export GRN_PLUGINS_DIR="$SRC/mayhem/build-test/plugins"

log="$(mktemp)"
python3 "$SRC/mayhem/oracle/run_oracle.py" "$GROONGA" "$SRC/test/command/suite" \
  "$SRC/mayhem/oracle/cases.txt" >"$log"
grep -v '^PASS' "$log" | head -40
passed="$(grep -c '^PASS ' "$log" || true)"
failed="$(grep -c '^FAIL ' "$log" || true)"
rm -f "$log"
if [ "$passed" -eq 0 ] && [ "$failed" -eq 0 ]; then failed=1; fi
emit_ctrf "groonga-command-suite" "$passed" "$failed" 0
