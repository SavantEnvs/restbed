#!/usr/bin/env bash
#
# mayhem/test.sh — run restbed's own Catch2 suites (built by mayhem/build.sh into
# build/mayhem-test/bin). A suite counts as passed only when it exits 0 AND Catch2
# printed its "All tests passed (N assertions in M test cases)" summary, so a
# program neutered to exit(0) fails every suite.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"
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

BIN=build/mayhem-test/bin
LIST=build/mayhem-test/suites.txt
if [ ! -s "$LIST" ]; then
  echo "test.sh: $LIST missing — run mayhem/build.sh first" >&2
  emit_ctrf catch2 0 1
  exit 1
fi

passed=0; failed=0
while read -r suite; do
  if [ ! -x "$BIN/$suite" ]; then
    echo "FAIL $suite (binary missing)"; failed=$((failed+1)); continue
  fi
  out=$("$BIN/$suite" 2>&1); rc=$?
  if [ $rc -eq 0 ] && grep -Eq '^All tests passed \([0-9]+ assertions? in [0-9]+ test cases?\)' <<<"$out"; then
    echo "PASS $suite: $(grep -E '^All tests passed' <<<"$out")"; passed=$((passed+1))
  else
    echo "FAIL $suite (exit $rc)"; tail -n 30 <<<"$out"; failed=$((failed+1))
  fi
done < "$LIST"

emit_ctrf catch2 "$passed" "$failed"
