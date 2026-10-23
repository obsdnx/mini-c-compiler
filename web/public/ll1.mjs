// LL(1) grammar analyser: nullable / FIRST / FOLLOW sets, the predictive parse
// table, LL(1) conflicts and left recursion. Runs in the browser (Grammar tab)
// and in Node (tools/ll1.mjs). No dependencies.

export const EPS = 'ε';
export const EOF = '$';

// Grammar text format:  lhs ::= sym sym | sym   with "|" continuation lines,
// quoted or UPPERCASE terminals, ε for the empty string and # comments.
export function parseGrammar(text) {
  const rules = [];
  let current = null;
  text.split('\n').forEach((raw, i) => {
    const line = raw.replace(/#.*$/, '').trim();
    if (!line) return;
    const m = line.match(/^([A-Za-z_][\w']*)\s*::=\s*(.*)$/);
    let body;
    if (m) {
      current = { lhs: m[1], alts: [] };
      rules.push(current);
      body = m[2];
    } else if (line.startsWith('|') && current) {
      body = line;
    } else {
      throw new Error(`line ${i + 1}: cannot parse "${raw.trim()}"`);
    }
    const syms = body.match(/"[^"]*"|[^\s|]+|\|/g) || [];
    let alt = [];
    let sawSymbol = false;
    const flush = () => {
      if (sawSymbol) current.alts.push(alt);
      alt = [];
      sawSymbol = false;
    };
    for (const s of syms) {
      if (s === '|') flush();
      else {
        sawSymbol = true;
        if (s !== EPS && s !== 'epsilon') alt.push(s);
      }
    }
    flush();
  });
  if (!rules.length) throw new Error('grammar is empty');

  // Merge repeated left-hand sides.
  const byName = new Map();
  for (const r of rules) {
    if (byName.has(r.lhs)) byName.get(r.lhs).alts.push(...r.alts);
    else byName.set(r.lhs, r);
  }
  const nonterminals = [...byName.keys()];
  const terminals = new Set();
  for (const r of byName.values())
    for (const alt of r.alts)
      for (const s of alt) {
        if (byName.has(s)) continue;
        if (!/^"/.test(s) && !/^[A-Z][A-Z0-9_]*$/.test(s))
          throw new Error(`"${s}" (in ${r.lhs}) is not defined and is not a terminal`);
        terminals.add(s);
      }
  return { start: nonterminals[0], nonterminals, terminals: [...terminals], rules: byName };
}

export const display = (s) => (s.startsWith('"') ? s.slice(1, -1) : s);

export function analyse(g) {
  const isNT = (s) => g.rules.has(s);
  const nullable = new Set();
  const first = new Map(g.nonterminals.map((n) => [n, new Set()]));
  const follow = new Map(g.nonterminals.map((n) => [n, new Set()]));
  follow.get(g.start).add(EOF);

  const firstOfSeq = (seq) => {
    const out = new Set();
    for (const s of seq) {
      if (!isNT(s)) {
        out.add(s);
        return { set: out, nullable: false };
      }
      for (const t of first.get(s)) out.add(t);
      if (!nullable.has(s)) return { set: out, nullable: false };
    }
    return { set: out, nullable: true };
  };

  // Fixed-point iteration for nullable and FIRST together.
  for (let changed = true; changed; ) {
    changed = false;
    for (const [A, r] of g.rules)
      for (const alt of r.alts) {
        const f = firstOfSeq(alt);
        for (const t of f.set)
          if (!first.get(A).has(t)) {
            first.get(A).add(t);
            changed = true;
          }
        if (f.nullable && !nullable.has(A)) {
          nullable.add(A);
          changed = true;
        }
      }
  }

  // FOLLOW: for A ::= α B β, FIRST(β) ⊆ FOLLOW(B), and FOLLOW(A) ⊆ FOLLOW(B) if β ⇒* ε.
  for (let changed = true; changed; ) {
    changed = false;
    for (const [A, r] of g.rules)
      for (const alt of r.alts)
        alt.forEach((B, i) => {
          if (!isNT(B)) return;
          const rest = firstOfSeq(alt.slice(i + 1));
          const add = [...rest.set, ...(rest.nullable ? follow.get(A) : [])];
          for (const t of add)
            if (!follow.get(B).has(t)) {
              follow.get(B).add(t);
              changed = true;
            }
        });
  }

  // Predictive parse table M[A][t] = list of alternatives; >1 entry = LL(1) conflict.
  const table = new Map(g.nonterminals.map((n) => [n, new Map()]));
  const conflicts = [];
  for (const [A, r] of g.rules)
    r.alts.forEach((alt, idx) => {
      const f = firstOfSeq(alt);
      const lookaheads = new Set([...f.set, ...(f.nullable ? follow.get(A) : [])]);
      for (const t of lookaheads) {
        const row = table.get(A);
        if (!row.has(t)) row.set(t, []);
        row.get(t).push(idx);
      }
    });
  for (const [A, row] of table)
    for (const [t, alts] of row)
      if (alts.length > 1) conflicts.push({ nonterminal: A, terminal: t, alternatives: alts });

  return { nullable, first, follow, table, conflicts, leftRecursion: findLeftRecursion(g, nullable) };
}

// A ⇒+ A ... : edge A → B when B can be the leftmost symbol of some A alternative.
function findLeftRecursion(g, nullable) {
  const edges = new Map(g.nonterminals.map((n) => [n, new Set()]));
  for (const [A, r] of g.rules)
    for (const alt of r.alts)
      for (const s of alt) {
        if (!g.rules.has(s)) break;
        edges.get(A).add(s);
        if (!nullable.has(s)) break;
      }
  const cycles = [];
  const seen = new Set();
  for (const start of g.nonterminals) {
    // Shortest path back to `start`, by BFS.
    const prev = new Map([[start, null]]);
    const queue = [start];
    let found = null;
    while (queue.length && !found) {
      const n = queue.shift();
      for (const m of edges.get(n)) {
        if (m === start) {
          found = n;
          break;
        }
        if (!prev.has(m)) {
          prev.set(m, n);
          queue.push(m);
        }
      }
    }
    if (!found) continue;
    const path = [start];
    for (let n = found; n !== start; n = prev.get(n)) path.splice(1, 0, n);
    const key = [...path].sort().join(',');
    if (seen.has(key)) continue;
    seen.add(key);
    cycles.push(path.concat(start));
  }
  return cycles;
}
