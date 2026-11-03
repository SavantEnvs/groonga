#!/usr/bin/env bash
#
# mayhem/build.sh — builds Groonga twice and the fuzz harnesses:
#   1. mayhem/build-fuzz  GRN_EMBED=ON static libgroonga + embedded plugins, with $SANITIZER_FLAGS,
#                         coverage instrumentation and DWARF 3 — linked into every harness.
#   2. mayhem/build-test  the normal GRN_EMBED=OFF Release build (no sanitizers) whose `groonga`
#                         executable and shared plugins are what mayhem/test.sh drives.
# Runs offline: every dependency is an apt -dev package (see mayhem/Dockerfile) and every option
# that would make CMake download something (FetchContent) is switched off.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
export CC CXX

cd "$SRC"

if [ ! -f vendor/onigmo-source/regint.h ] && [ -z "$(ls -A vendor/onigmo-source 2>/dev/null)" ]; then
  echo "vendor/onigmo-source is empty: check the repo out with submodules (git submodule update --init vendor/onigmo-source)" >&2
  exit 1
fi

# Groonga's `alignment` UBSan check fires on its packed on-disk/hash value layout throughout the
# library, and `pointer-overflow` on NULL+0 arithmetic over empty buffers; both are benign and would
# halt almost every input, so only these two checks are relaxed.
UBSAN_RELAX="-fno-sanitize=alignment,pointer-overflow"

CMAKE_OFFLINE=(
  -DGRN_WITH_OPENZL=no -DGRN_WITH_XSIMD=no -DGRN_WITH_H3=no -DGRN_WITH_LLAMA_CPP=no
  -DGRN_WITH_FAISS=no -DGRN_WITH_USEARCH=no -DGRN_WITH_BASE64=no -DGRN_WITH_JSONCONS=no
  -DGRN_WITH_BLOSC=no -DGRN_WITH_CURL=no -DGRN_WITH_ROARING_BITMAPS=no -DGRN_WITH_LIBEDIT=no
  -DGRN_WITH_MRUBY=no
)

# CMake discards -D options when it notices a changed compiler path (the image build runs under
# ccache's masquerade dir, a re-run does not), so always configure from a fresh cache.
rm -f mayhem/build-fuzz/CMakeCache.txt mayhem/build-test/CMakeCache.txt

# 1) Sanitized embedded build for the harnesses.
FUZZ_FLAGS="$SANITIZER_FLAGS $UBSAN_RELAX $DEBUG_FLAGS -O1 -fsanitize=fuzzer-no-link"
cmake -S . -B mayhem/build-fuzz -G Ninja -DGRN_EMBED=ON -DCMAKE_BUILD_TYPE=None \
  -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_C_FLAGS="$FUZZ_FLAGS" -DCMAKE_CXX_FLAGS="$FUZZ_FLAGS" "${CMAKE_OFFLINE[@]}"
cmake --build mayhem/build-fuzz -j"$MAYHEM_JOBS"

# 2) The harnesses, as libFuzzer targets and as standalone run-once reproducers.
B=mayhem/build-fuzz
LIBS="$(find $B/plugins -name '*.a' | sort | tr '\n' ' ')"
SYSLIBS="-lz -lzstd -llz4 -lmsgpack-c -lxxhash -lstemmer -lsimdjson -lm -lpthread -ldl"
HARNESS_FLAGS="$SANITIZER_FLAGS $UBSAN_RELAX $DEBUG_FLAGS"

$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o /tmp/lsan_off.o
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o /tmp/standalone_main.o
$CC -c mayhem/sancov_stubs.c -o /tmp/sancov_stubs.o

for t in fuzz_command fuzz_expr fuzz_load fuzz_tokenize; do
  $CC $HARNESS_FLAGS -DGRN_STATIC -Iinclude -I$B/include -c "mayhem/$t.c" -o "/tmp/$t.o"
  # clang++ drives the link: Groonga's C++ sources use UBSan's vptr checks.
  $CXX $HARNESS_FLAGS $LIB_FUZZING_ENGINE "/tmp/$t.o" /tmp/lsan_off.o -o "/mayhem/$t" \
    -Wl,--start-group $B/lib/libgroonga.a $LIBS $B/vendor/onigmo/libonigmo.a -Wl,--end-group $SYSLIBS
  $CXX $HARNESS_FLAGS /tmp/standalone_main.o /tmp/sancov_stubs.o "/tmp/$t.o" /tmp/lsan_off.o -o "/mayhem/$t-standalone" \
    -Wl,--start-group $B/lib/libgroonga.a $LIBS $B/vendor/onigmo/libonigmo.a -Wl,--end-group $SYSLIBS
done

# 3) The normal-flags build test.sh drives (no sanitizers, shared plugins).
cmake -S . -B mayhem/build-test -G Ninja -DGRN_EMBED=OFF -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_C_FLAGS="$COVERAGE_FLAGS" -DCMAKE_CXX_FLAGS="$COVERAGE_FLAGS" "${CMAKE_OFFLINE[@]}"
cmake --build mayhem/build-test -j"$MAYHEM_JOBS"
