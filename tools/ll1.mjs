#!/usr/bin/env node
// Usage: node tools/ll1.mjs grammar/minic-ll1.bnf [--sets]
// Reports left recursion and LL(1) conflicts; --sets also prints FIRST/FOLLOW.
import { readFileSync } from 'node:fs';
import { analyse, display, parseGrammar, EOF } from '../web/public/ll1.mjs';

const [file, flag] = process.argv.slice(2);
if (!file) {
  console.error('usage: node tools/ll1.mjs <grammar.bnf> [--sets]');
  process.exit(2);
}
const g = parseGrammar(readFileSync(file, 'utf8'));
const a = analyse(g);
const set = (s) => '{ ' + [...s].map(display).sort().join(', ') + ' }';

console.log(`${file}: ${g.nonterminals.length} non-terminals, ${g.terminals.length} terminals\n`);

if (flag === '--sets') {
  const w = Math.max(...g.nonterminals.map((n) => n.length));
  console.log('NON-TERMINAL'.padEnd(w + 2) + 'NULLABLE  FIRST / FOLLOW');
  for (const n of g.nonterminals) {
    console.log(`${n.padEnd(w + 2)}${(a.nullable.has(n) ? 'yes' : '').padEnd(10)}FIRST  ${set(a.first.get(n))}`);
    console.log(`${''.padEnd(w + 12)}FOLLOW ${set(a.follow.get(n))}`);
  }
  console.log();
}

if (a.leftRecursion.length) {
  console.log(`Left recursion (${a.leftRecursion.length}) — a top-down parser would loop forever:`);
  for (const c of a.leftRecursion) console.log('  ' + c.join(' ⇒ '));
  console.log();
}

if (a.conflicts.length === 0) {
  console.log('No LL(1) conflicts: the grammar is LL(1).');
} else {
  console.log(`LL(1) conflicts (${a.conflicts.length}):`);
  for (const c of a.conflicts) {
    const alts = c.alternatives.map((i) => {
      const alt = g.rules.get(c.nonterminal).alts[i];
      return alt.length ? alt.map(display).join(' ') : 'ε';
    });
    console.log(`  M[${c.nonterminal}, ${display(c.terminal === EOF ? '$' : c.terminal)}] = ${alts.join('  |  ')}`);
  }
}
process.exit(0);
