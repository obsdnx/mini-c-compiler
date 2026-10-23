#!/usr/bin/env bash
# Runs mccomp's Linux AArch64 output natively in an arm64 Ubuntu container,
# assembled and linked with GNU binutils (gcc) rather than clang.
# Needs Docker on an arm64 host (e.g. Apple silicon).
set -eu
cd "$(dirname "$0")/.."
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
for f in tests/programs/*.c; do
  n=$(basename "$f" .c)
  ./build/mccomp "$f" --emit=asm --target=linux -o "$OUT/$n.s"
  cp "${f%.c}.expected" "$OUT/$n.expected"
done
cp runtime/runtime.c "$OUT/"
docker run --rm --platform linux/arm64 -v "$OUT:/w" -w /w ubuntu:24.04 bash -c '
  apt-get update -qq >/dev/null && apt-get install -y -qq gcc >/dev/null 2>&1
  pass=0; fail=0
  for s in *.s; do
    n=${s%.s}
    if gcc "$s" runtime.c -lm -o "$n.bin" && ./"$n.bin" | diff -q - "$n.expected" >/dev/null; then
      pass=$((pass + 1))
    else
      echo "FAIL $n"; fail=$((fail + 1))
    fi
  done
  echo "Linux AArch64 ($(uname -m), GNU as/ld): $pass passed, $fail failed"
  [ $fail -eq 0 ]'
