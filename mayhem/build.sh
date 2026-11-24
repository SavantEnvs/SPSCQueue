#!/usr/bin/env bash
# mayhem/build.sh — build the SPSCQueue fuzz harness (+ standalone reproducer) and the
# upstream test suite. Runs in /mayhem as `mayhem`; no network needed (header-only, no deps).
#
# SPSCQueue is a single header (include/rigtorp/SPSCQueue.h): the "project" is compiled
# into each translation unit, so building the harness with $SANITIZER_FLAGS instruments it.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
INC=(-I"$SRC/include")
STD=(-std=c++20)
OBJ="$SRC/build/mayhem-obj"     # build/ is gitignored upstream
mkdir -p "$OBJ"

# 1+2) Fuzz harness (header compiled in with the sanitizers) and its standalone reproducer.
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS "${STD[@]}" -c "$SRC/mayhem/lsan_off.cc" -o "$OBJ/lsan_off.o"
$CC  $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OBJ/standalone_main.o"
# The fuzzer object gets SanCov (-fsanitize=fuzzer-no-link); the standalone one does not, so it
# links without the libFuzzer runtime (and with no sanitizer runtime at all when SANITIZER_FLAGS=).
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link "${STD[@]}" -O1 "${INC[@]}" \
    -c "$SRC/mayhem/fuzz_spscqueue.cpp" -o "$OBJ/fuzz_spscqueue.o"
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS "${STD[@]}" -O1 "${INC[@]}" \
    -c "$SRC/mayhem/fuzz_spscqueue.cpp" -o "$OBJ/fuzz_spscqueue-standalone.o"
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$OBJ/fuzz_spscqueue.o" "$OBJ/lsan_off.o" \
    -o "$SRC/fuzz_spscqueue"
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$OBJ/standalone_main.o" "$OBJ/fuzz_spscqueue-standalone.o" \
    "$OBJ/lsan_off.o" -o "$SRC/fuzz_spscqueue-standalone"

# 3) Test suite with the project's normal flags: upstream's SPSCQueueTest (asserts are
#    kept on, as in upstream's Debug CI) and a known-answer driver whose output test.sh diffs.
mkdir -p "$SRC/build/mayhem-tests"
$CXX "${STD[@]}" -O1 $COVERAGE_FLAGS "${INC[@]}" -pthread "$SRC/src/SPSCQueueTest.cpp" \
    -o "$SRC/build/mayhem-tests/SPSCQueueTest"
$CXX "${STD[@]}" -O1 $COVERAGE_FLAGS "${INC[@]}" "$SRC/mayhem/spsc_kat.cpp" \
    -o "$SRC/build/mayhem-tests/spsc_kat"
