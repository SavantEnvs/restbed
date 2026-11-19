#!/usr/bin/env bash
#
# mayhem/build.sh — build restbed's fuzz harnesses and its Catch2 test suite.
#
# Upstream builds with autotools; the library is 16 C++23 translation units plus
# header-only standalone asio (no generated sources), so this compiles them
# directly — the same sources, defines and include paths as Makefile.am
# (librestbed_la_SOURCES, -DASIO_STANDALONE=1, -Isrc), SSL and IPC off (the
# configure defaults' optional features) — once sanitized for the harnesses and
# once with normal flags for the tests. Everything lands under build/ (gitignored
# upstream), so `git clean -ffdX` resets it. Re-runnable and offline: asio and
# Catch2 come from apt packages baked into the image.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

COMMON="-std=c++23 -pthread -DASIO_STANDALONE=1 -Isrc"
LIB_SOURCES=(src/corvusoft/restbed/*.cpp src/corvusoft/restbed/detail/*.cpp)

# build_lib <compiler> <outdir> <flags...>: compile every library TU into <outdir>/librestbed.a
build_lib() {
  local cxx="$1" out="$2"; shift 2
  local flags="$*"
  mkdir -p "$out/obj"
  rm -f "$out"/obj/*.o "$out/librestbed.a"
  printf '%s\n' "${LIB_SOURCES[@]}" | \
    xargs -P "$MAYHEM_JOBS" -I{} sh -c \
      'f="$1"; shift; o="$0/obj/$(echo "$f" | tr / _ ).o"; exec '"$cxx"' '"$COMMON $flags"' -c "$f" -o "$o"' \
      "$out" {}
  ar rcs "$out/librestbed.a" "$out"/obj/*.o
}

# 1) Sanitized + coverage-instrumented library for the harnesses.
FUZZ_DIR=build/mayhem-fuzz
FUZZ_FLAGS="-O1 $SANITIZER_FLAGS -fsanitize=fuzzer-no-link $DEBUG_FLAGS"
build_lib "$CXX" "$FUZZ_DIR" $FUZZ_FLAGS

# 2) Harnesses: libFuzzer binary + standalone reproducer each.
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$FUZZ_DIR/standalone_main.o"
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o "$FUZZ_DIR/lsan_off.o"
HARNESSES="fuzz_http_request fuzz_websocket fuzz_uri fuzz_string fuzz_publish"
printf '%s\n' $HARNESSES | xargs -P "$MAYHEM_JOBS" -I{} \
  $CXX $COMMON $FUZZ_FLAGS -c "mayhem/harnesses/{}.cpp" -o "$FUZZ_DIR/{}.o"
for h in $HARNESSES; do
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE -pthread \
    "$FUZZ_DIR/$h.o" "$FUZZ_DIR/lsan_off.o" "$FUZZ_DIR/librestbed.a" -o "$SRC/$h"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS -pthread \
    "$FUZZ_DIR/standalone_main.o" "$FUZZ_DIR/$h.o" "$FUZZ_DIR/lsan_off.o" "$FUZZ_DIR/librestbed.a" \
    -o "$SRC/$h-standalone"
done

# 3) Upstream's Catch2 suite (test/Makefile.am check_PROGRAMS, minus the IPC-only
#    suite) with normal flags; mayhem/test.sh runs build/mayhem-test/bin/*.
#    Built with GCC (upstream's documented compiler, GCC 13+): clang-19 with
#    libstdc++-14 rejects Catch2's REQUIRE( tuple == tuple ) decomposition in the
#    web_socket_message suites (std::tuple operator<=> on Catch::Decomposer).
: "${TEST_CXX:=g++}"
TEST_DIR=build/mayhem-test
build_lib "$TEST_CXX" "$TEST_DIR" -O1 $COVERAGE_FLAGS
mkdir -p "$TEST_DIR/bin"
rm -f "$TEST_DIR"/bin/*
SUITES=$(sed -n '/^check_PROGRAMS = /,/^$/p' test/Makefile.am | grep -o '[a-z_]*_suite' )
printf '%s\n' $SUITES > "$TEST_DIR/suites.txt"
printf '%s\n' $SUITES | xargs -P "$MAYHEM_JOBS" -I{} \
  $TEST_CXX $COMMON -O1 $COVERAGE_FLAGS test/{}.cpp "$TEST_DIR/librestbed.a" -lCatch2 -o "$TEST_DIR/bin/{}"
echo "built $(wc -l < "$TEST_DIR/suites.txt") test suites"
