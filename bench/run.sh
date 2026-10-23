#!/usr/bin/env bash
# Benchmarks the three code paths on each bench/*.c program and prints a
# markdown table (best of 3 wall-clock runs, plus static instruction counts).
set -eu
cd "$(dirname "$0")/.."
MCC=./build/mccomp
LLVM_CONFIG=${LLVM_CONFIG:-$(command -v llvm-config || echo /opt/homebrew/opt/llvm/bin/llvm-config)}
LLVM_BIN=$("$LLVM_CONFIG" --bindir)
CC=${CC:-$LLVM_BIN/clang}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

best_of_3() {
  local best=999999
  for _ in 1 2 3; do
    local s e
    s=$(perl -MTime::HiRes=time -e 'printf "%d", time*1000')
    "$1" >/dev/null
    e=$(perl -MTime::HiRes=time -e 'printf "%d", time*1000')
    (( e - s < best )) && best=$((e - s))
  done
  echo $best
}
# Instructions in the program's own functions (excludes directives, labels, comments).
count_insns() { grep -E '^\s+[a-z]' "$1" | grep -vE '^\s+\.' | wc -l | tr -d ' '; }

echo "| program | mccomp AArch64 | LLVM -O0 | LLVM -O2 | instrs (mccomp / LLVM -O0 / LLVM -O2) |"
echo "|---|---:|---:|---:|---:|"
for src in bench/*.c; do
  n=$(basename "$src" .c)
  $MCC "$src" --emit=asm -o "$TMP/$n.s"
  $CC "$TMP/$n.s" runtime/runtime.c -lm -o "$TMP/$n.mine"
  $MCC "$src" --emit=ir -o "$TMP/$n.ll"
  $CC -w -O0 "$TMP/$n.ll" runtime/runtime.c -o "$TMP/$n.o0"
  $CC -w -O0 -S "$TMP/$n.ll" -o "$TMP/$n.o0.s"
  $MCC "$src" --emit=llvm-asm -o "$TMP/$n.o2.s"
  $CC "$TMP/$n.o2.s" runtime/runtime.c -lm -o "$TMP/$n.o2"
  a=$("$TMP/$n.mine"); b=$("$TMP/$n.o0"); c=$("$TMP/$n.o2")
  [ "$a" = "$b" ] && [ "$b" = "$c" ] || { echo "output mismatch on $n: $a / $b / $c" >&2; exit 1; }
  echo "| $n | $(best_of_3 "$TMP/$n.mine") ms | $(best_of_3 "$TMP/$n.o0") ms | $(best_of_3 "$TMP/$n.o2") ms | $(count_insns "$TMP/$n.s") / $(count_insns "$TMP/$n.o0.s") / $(count_insns "$TMP/$n.o2.s") |"
done
