#!/usr/bin/env bash
# Differential test-suite.
#
#  tests/programs/*.c  are compiled three independent ways and every binary's
#                      stdout must equal the checked-in .expected file:
#                        llvm-O0   mccomp -> LLVM IR            -> clang
#                        llvm-O2   mccomp -> LLVM IR -> O2 pipeline -> clang
#                        aarch64   mccomp's own AArch64 backend -> assembler
#  tests/errors/*.c    must be rejected; stderr must contain the text on the
#                      file's first line ("// expect: ...").
set -u
cd "$(dirname "$0")/.."

MCC=./build/mccomp
# The .ll files must be read by a clang from the same LLVM release that wrote them.
LLVM_CONFIG=${LLVM_CONFIG:-$(command -v llvm-config || echo /opt/homebrew/opt/llvm/bin/llvm-config)}
LLVM_BIN=$("$LLVM_CONFIG" --bindir)
CC=${CC:-$([ -x "$LLVM_BIN/clang" ] && echo "$LLVM_BIN/clang" || echo clang)}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

GREEN=$'\033[32m'; RED=$'\033[31m'; DIM=$'\033[2m'; RESET=$'\033[0m'
[ -t 1 ] || { GREEN=; RED=; DIM=; RESET=; }

pass=0
fail=0
failures=()

ok()  { pass=$((pass + 1)); printf "  ${GREEN}✓${RESET} %-18s" "$1"; }
bad() { fail=$((fail + 1)); failures+=("$1"); printf "  ${RED}✗${RESET} %-18s" "$1"; }

case "$(uname -m)" in
  arm64|aarch64) NATIVE_ARM=1 ;;
  *) NATIVE_ARM=0 ;;
esac
[ "$(uname -s)" = Darwin ] && OS=darwin || OS=linux

build() { # mode src out
  local mode=$1 src=$2 out=$3
  case $mode in
    llvm-O0) $MCC "$src" --emit=ir -o "$out.ll" 2>/dev/null && $CC -w "$out.ll" runtime/runtime.c -lm -o "$out" ;;
    llvm-O2) $MCC "$src" --emit=ir-opt -o "$out.ll" 2>/dev/null && $CC -w "$out.ll" runtime/runtime.c -lm -o "$out" ;;
    aarch64) $MCC "$src" --emit=asm -o "$out.s" 2>/dev/null && $CC "$out.s" runtime/runtime.c -lm -o "$out" ;;
  esac
}

echo "Programs (output must match .expected under every backend)"
for src in tests/programs/*.c; do
  name=$(basename "$src" .c)
  expected="tests/programs/$name.expected"
  printf "%-20s" "$name"
  modes="llvm-O0 llvm-O2"
  [ $NATIVE_ARM = 1 ] && modes="$modes aarch64"
  for mode in $modes; do
    bin="$TMP/$name.$mode"
    if ! build "$mode" "$src" "$bin" >"$TMP/log" 2>&1; then
      bad "$mode"; failures[${#failures[@]}-1]="$name [$mode]: build failed: $(head -3 "$TMP/log")"
      continue
    fi
    if diff -u "$expected" <("$bin") >"$TMP/diff" 2>&1; then
      ok "$mode"
    else
      bad "$mode"; failures[${#failures[@]}-1]="$name [$mode]: output differs
$(cat "$TMP/diff")"
    fi
  done
  # The other AArch64 flavour can't run here, but it must at least assemble.
  other=$([ $OS = darwin ] && echo linux || echo darwin)
  triple=$([ $other = linux ] && echo aarch64-linux-gnu || echo arm64-apple-macos)
  if $MCC "$src" --emit=asm --target=$other -o "$TMP/$name.$other.s" 2>/dev/null &&
     $CC --target=$triple -c "$TMP/$name.$other.s" -o "$TMP/$name.$other.o" 2>"$TMP/log"; then
    ok "asm($other)"
  else
    bad "asm($other)"; failures[${#failures[@]}-1]="$name [asm $other]: $(head -3 "$TMP/log")"
  fi
  echo
done

echo
echo "Diagnostics (must be rejected with the expected message)"
for src in tests/errors/*.c; do
  name=$(basename "$src" .c)
  want=$(head -1 "$src" | sed 's#^// expect: ##')
  printf "%-24s" "$name"
  if $MCC "$src" -o "$TMP/err.ll" >/dev/null 2>"$TMP/err"; then
    bad "accepted"; failures[${#failures[@]}-1]="$name: compiled but should have failed"
  elif grep -qF -- "$want" "$TMP/err"; then
    ok "rejected"; printf "${DIM}%s${RESET}" "$(head -1 "$TMP/err" | sed 's/.*error: //')"
  else
    bad "wrong error"; failures[${#failures[@]}-1]="$name: expected '$want', got: $(head -1 "$TMP/err")"
  fi
  echo
done

echo
echo "Grammar (tools/ll1.mjs)"
if command -v node >/dev/null; then
  printf "%-24s" "minic-ll1.bnf"
  if node tools/ll1.mjs grammar/minic-ll1.bnf >"$TMP/ll1" 2>&1 &&
     grep -q "LL(1) conflicts (1)" "$TMP/ll1" && grep -q "M\[expr, IDENT\]" "$TMP/ll1" &&
     ! grep -q "Left recursion" "$TMP/ll1"; then
    ok "LL(1) except expr/IDENT"
  else
    bad "LL(1) check"; failures[${#failures[@]}-1]="grammar: $(cat "$TMP/ll1")"
  fi
  echo
  # Every non-terminal in the grammar must have a matching parse-tree rule in the parser.
  printf "%-24s" "parser ↔ grammar"
  missing=$(node -e '
    const fs = require("fs");
    const g = fs.readFileSync("grammar/minic-ll1.bnf", "utf8").replace(/#.*$/gm, "");
    const src = fs.readFileSync("src/parser.cpp", "utf8");
    const nts = [...g.matchAll(/^([A-Za-z_][\w\x27]*)\s*::=/gm)].map(m => m[1]);
    console.log(nts.filter(n => !src.includes("\"" + n + "\"")).join(" "));')
  if [ -z "$missing" ]; then ok "all rules implemented"; else bad "missing"; failures[${#failures[@]}-1]="parser lacks rules: $missing"; fi
  echo
else
  echo "  (node not installed, skipped)"
fi

echo
if [ $fail -eq 0 ]; then
  echo "${GREEN}All $pass checks passed.${RESET}"
else
  echo "${RED}$fail of $((pass + fail)) checks failed:${RESET}"
  for f in "${failures[@]}"; do echo "--- $f"; done
  exit 1
fi
