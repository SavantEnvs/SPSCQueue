#!/usr/bin/env bash
# mayhem/test.sh — RUN the suites mayhem/build.sh built (never compiles):
#   1. upstream's src/SPSCQueueTest (CHECK()-based functional/death/concurrency tests)
#   2. mayhem/spsc_kat: known-answer run whose stdout must equal mayhem/spsc_kat.expected
# Emits a CTRF report; exit non-zero iff any test failed.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SRC:=/mayhem}"
cd "$SRC"

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

T="$SRC/build/mayhem-tests"
for b in SPSCQueueTest spsc_kat; do
  [ -x "$T/$b" ] || { echo "missing $T/$b — run mayhem/build.sh first" >&2; emit_ctrf spscqueue 0 2; exit 1; }
done

passed=0 failed=0

# Upstream's test prints nothing on success and abort()s on a failed CHECK().
out="$("$T/SPSCQueueTest" 2>&1)"; rc=$?
if [ $rc -eq 0 ]; then
  echo "PASS SPSCQueueTest"; passed=$((passed+1))
else
  echo "FAIL SPSCQueueTest (rc=$rc)"; echo "$out" | tail -20; failed=$((failed+1))
fi

if diff -u "$SRC/mayhem/spsc_kat.expected" <("$T/spsc_kat" 2>&1); then
  echo "PASS spsc_kat"; passed=$((passed+1))
else
  echo "FAIL spsc_kat"; failed=$((failed+1))
fi

emit_ctrf spscqueue "$passed" "$failed"
