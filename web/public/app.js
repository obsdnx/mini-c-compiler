import { renderTree, compressChains } from './tree.js';
import { parseGrammar, analyse, display, EOF } from './ll1.mjs';

const $ = (s) => document.querySelector(s);
const h = (tag, attrs = {}, ...kids) => {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === 'class') e.className = v;
    else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else if (k === 'html') e.innerHTML = v;
    else e.setAttribute(k, v);
  }
  for (const k of kids.flat()) if (k != null) e.append(k instanceof Node ? k : document.createTextNode(String(k)));
  return e;
};
const esc = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

const state = {
  target: localStorage.getItem('mccomp.target') || 'darwin',
  tab: localStorage.getItem('mccomp.tab') || 'ast',
  result: null,
  run: null,
  compressCst: true,
  hideDirectives: true,
  grammar: 'minic-ll1.bnf',
  info: { nativeArm: true },
};

//===--- Editor --------------------------------------------------------------===//

const src = $('#source');
const hl = $('#highlight');
const gutter = $('#gutter');
const marks = $('#marks');
const LINE = 20;
const PAD_TOP = 10;

const KEYWORDS = new Set(['extern', 'if', 'else', 'while', 'return']);
const TYPES = new Set(['int', 'float', 'bool', 'void']);
const LEX = /(\/\/[^\n]*|\/\*[\s\S]*?(?:\*\/|$))|([A-Za-z_]\w*)|(\d+\.\d*|\.\d+|\d+)|(&&|\|\||==|!=|<=|>=|[-+*/%!<>=])|([(){};,])/g;

function highlightSource(text) {
  let out = '', last = 0, m;
  LEX.lastIndex = 0;
  while ((m = LEX.exec(text))) {
    out += esc(text.slice(last, m.index));
    const [tok, comment, word, num, op, punct] = m;
    const cls = comment ? 'comment' : word ? (KEYWORDS.has(word) ? 'keyword' : TYPES.has(word) ? 'type' : word === 'true' || word === 'false' ? 'literal' : 'ident')
      : num ? 'literal' : op ? 'op' : punct ? 'punct' : '';
    out += `<span class="t-${cls}">${esc(tok)}</span>`;
    last = m.index + tok.length;
  }
  return out + esc(text.slice(last)) + '\n';
}

function renderEditor() {
  const text = src.value;
  hl.innerHTML = highlightSource(text);
  const lines = text.split('\n').length;
  const diagLines = new Map();
  for (const d of state.result?.diagnostics || []) if (d.line) diagLines.set(d.line, d.severity === 'error' ? 'err' : diagLines.get(d.line) || 'warn');
  gutter.innerHTML = '';
  for (let i = 1; i <= lines; i++) gutter.append(h('div', { class: diagLines.get(i) || '' }, i));
  drawDiagnosticMarks();
}

function lineCol(index) {
  const before = src.value.slice(0, index).split('\n');
  return { line: before.length, col: before[before.length - 1].length + 1 };
}
function indexOf(line, col) {
  const lines = src.value.split('\n');
  let i = 0;
  for (let l = 1; l < line && l <= lines.length; l++) i += lines[l - 1].length + 1;
  return i + col - 1;
}

function mark(line, col, len, cls) {
  const m = h('div', { class: 'mark ' + cls });
  m.style.top = (line - 1) * LINE + PAD_TOP + 'px';
  if (!cls.startsWith('line')) {
    m.style.left = `calc(4px + ${col - 1}ch)`;
    m.style.width = `${Math.max(len, 1)}ch`;
  }
  marks.append(m);
  return m;
}
function drawDiagnosticMarks() {
  marks.querySelectorAll('.err,.warn').forEach((m) => m.remove());
  for (const d of state.result?.diagnostics || []) {
    if (!d.line) continue;
    const tok = tokenAt(d.line, d.col);
    mark(d.line, d.col, tok ? tok.lexeme.length : 1, d.severity === 'error' ? 'err' : 'warn');
  }
}
function clearHover() {
  marks.querySelectorAll('.hover').forEach((m) => m.remove());
}
function hoverSource(line, col, len) {
  clearHover();
  if (!line) return;
  mark(line, 1, 0, 'line hover');
  if (col) mark(line, col, len ?? (tokenAt(line, col)?.lexeme.length || 1), 'hover');
  const editor = $('#editor');
  const y = (line - 1) * LINE;
  if (y < editor.scrollTop || y > editor.scrollTop + editor.clientHeight - 40) editor.scrollTop = y - editor.clientHeight / 3;
}
function tokenAt(line, col) {
  return state.result?.tokens?.find((t) => t.line === line && t.col === col);
}
function jumpTo(line, col) {
  const i = indexOf(line, col || 1);
  src.focus();
  src.setSelectionRange(i, i + (tokenAt(line, col)?.lexeme.length || 0));
  hoverSource(line, col);
}

src.addEventListener('input', () => {
  renderEditor();
  scheduleCompile();
});
src.addEventListener('keydown', (e) => {
  if (e.key === 'Tab') {
    e.preventDefault();
    document.execCommand('insertText', false, '  ');
  } else if (e.key === 'Enter' && (e.metaKey || e.ctrlKey)) {
    e.preventDefault();
    runProgram();
  }
});
// Hovering a source line lights up the assembly generated for it.
src.addEventListener('mousemove', (e) => {
  if (state.tab !== 'asm') return;
  const r = src.getBoundingClientRect();
  const line = Math.floor((e.clientY - r.top - PAD_TOP) / LINE) + 1;
  highlightAsmLine(line);
});
src.addEventListener('mouseleave', () => highlightAsmLine(null));

//===--- Compile -------------------------------------------------------------===//

let timer = null, seq = 0;
function scheduleCompile() {
  clearTimeout(timer);
  timer = setTimeout(compile, 250);
}
async function compile() {
  const my = ++seq;
  try {
    const res = await fetch('/api/compile', {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ source: src.value, target: state.target }),
    });
    const data = await res.json();
    if (my !== seq) return;
    if (data.error) throw new Error(data.error);
    state.result = data;
    state.run = null;
  } catch (e) {
    if (my !== seq) return;
    state.result = { ok: false, diagnostics: [{ severity: 'error', stage: 'server', message: String(e.message || e), line: 0, col: 0 }] };
  }
  renderEditor();
  renderDiagnostics();
  renderPipeline();
  renderTabs();
  renderPanel();
}

//===--- Pipeline + diagnostics ---------------------------------------------===//

const countInstrs = (asm) => (asm || '').split('\n').filter((l) => /^\s+[a-z]/.test(l) && !/^\s+\./.test(l)).length;
const lines = (s) => (s ? s.trimEnd().split('\n').length : 0);
const us = (n) => (n == null ? '' : n >= 1000 ? (n / 1000).toFixed(1) + ' ms' : n + ' µs');

function stageStatus(r, stage, field) {
  if (r?.diagnostics?.some((d) => d.severity === 'error' && d.stage === stage)) return 'error';
  return r?.[field] ? 'ok' : 'skipped';
}

function renderPipeline() {
  const r = state.result || {};
  const t = r.timings || {};
  const warnings = (r.diagnostics || []).filter((d) => d.severity === 'warning').length;
  const meta = (...parts) => parts.filter(Boolean).join(' · ');
  const stages = [
    { name: 'Lexer', tab: 'tokens', status: stageStatus(r, 'lexer', 'tokens'), meta: r.tokens ? meta(`${r.tokens.length} tokens`, us(t.lex)) : '' },
    { name: 'Parser', tab: 'parse', status: stageStatus(r, 'parser', 'ast'), meta: r.parseTree ? meta('LL(1) recursive descent', us(t.parse)) : '' },
    { name: 'Semantic analysis', tab: 'ast', status: stageStatus(r, 'semantic', 'symbols'), meta: r.symbols ? meta(`${r.symbols.length} symbols`, warnings && `${warnings} warning${warnings > 1 ? 's' : ''}`, us(t.sema)) : '' },
    { name: 'LLVM IR → O2', tab: 'ir', status: stageStatus(r, 'codegen', 'ir'), meta: r.ir ? meta(`${lines(r.ir)} → ${lines(r.irOpt)} lines`, us(t.llvm)) : '' },
    { name: 'AArch64 backend', tab: 'asm', status: stageStatus(r, 'codegen', 'asm'), meta: r.asm ? meta(`${countInstrs(r.asm)} instrs`, us(t.aarch64)) : '' },
  ];
  const nav = $('#pipeline');
  nav.innerHTML = '';
  stages.forEach((s, i) => {
    if (i) nav.append(h('span', { class: 'arrow' }, '→'));
    nav.append(h('div', { class: `stage ${s.status}`, onclick: () => selectTab(s.tab), title: s.status }, h('span', { class: 'dot' }), h('span', { class: 'name' }, s.name), h('span', { class: 'meta' }, s.meta)));
  });
}

function renderDiagnostics() {
  const box = $('#diagnostics');
  box.innerHTML = '';
  for (const d of state.result?.diagnostics || [])
    box.append(h('div', { class: `diag ${d.severity}`, onclick: () => d.line && jumpTo(d.line, d.col), onmouseenter: () => hoverSource(d.line, d.col), onmouseleave: clearHover },
      h('span', { class: 'sev' }, d.severity),
      h('span', { class: 'where' }, d.line ? `${d.line}:${d.col}` : ''),
      h('span', {}, d.message, ' ', h('span', { class: 'stage-tag' }, `[${d.stage}]`))));
}

//===--- Tabs ----------------------------------------------------------------===//

const TABS = [
  { id: 'tokens', label: 'Tokens', count: (r) => r?.tokens?.length },
  { id: 'parse', label: 'Parse tree' },
  { id: 'ast', label: 'AST' },
  { id: 'symbols', label: 'Symbols', count: (r) => r?.symbols?.length },
  { id: 'ir', label: 'LLVM IR' },
  { id: 'asm', label: 'AArch64' },
  { id: 'run', label: 'Run' },
  { id: 'grammar', label: 'Grammar' },
];

function selectTab(id) {
  state.tab = id;
  localStorage.setItem('mccomp.tab', id);
  renderTabs();
  renderPanel();
}
function renderTabs() {
  const bar = $('#tabs');
  bar.innerHTML = '';
  for (const t of TABS) {
    const n = t.count?.(state.result);
    bar.append(h('button', { class: 'tab' + (state.tab === t.id ? ' on' : ''), role: 'tab', onclick: () => selectTab(t.id) }, t.label, n ? h('span', { class: 'count' }, n) : null));
  }
}
function renderPanel() {
  const panel = $('#panel');
  panel.innerHTML = '';
  clearHover();
  const r = state.result;
  if (!r) return panel.append(h('div', { class: 'empty' }, 'Compiling…'));
  ({ tokens: tokensPanel, parse: parsePanel, ast: astPanel, symbols: symbolsPanel, ir: irPanel, asm: asmPanel, run: runPanel, grammar: grammarPanel })[state.tab](panel, r);
}
const notAvailable = (panel, what) => panel.append(h('div', { class: 'empty' }, `No ${what}: compilation stopped at an earlier stage. See the error under the editor.`));

//===--- Tokens ----------------------------------------------------------------===//

function tokenClass(kind) {
  if (['extern', 'if', 'else', 'while', 'return'].includes(kind)) return 'keyword';
  if (['int', 'float', 'bool', 'void'].includes(kind)) return 'type';
  if (kind.endsWith('_LIT')) return 'literal';
  if (kind === 'IDENT') return 'ident';
  if (kind === 'EOF') return 'punct';
  if ('(){};,'.includes(kind)) return 'punct';
  return 'op';
}

function tokensPanel(panel, r) {
  if (!r.tokens) return notAvailable(panel, 'tokens');
  panel.append(h('div', { class: 'panel-pad' }, h('p', { class: 'explain', html: 'The lexer is a hand-written <b>maximal-munch</b> scanner (<code>src/lexer.cpp</code>): it always takes the longest match (<code>&lt;=</code> not <code>&lt;</code> then <code>=</code>), and keywords take priority over identifiers. Every token records its line and column for error messages. Hover a token to find it in the source.' })));
  const counts = {};
  for (const t of r.tokens) counts[tokenClass(t.kind)] = (counts[tokenClass(t.kind)] || 0) + 1;
  panel.append(h('div', { class: 'stats' }, Object.entries(counts).map(([k, v]) => h('span', {}, h('b', { class: 't-' + k }, v), ' ', k))));
  const box = h('div', { class: 'tokens' });
  for (const t of r.tokens) {
    const c = tokenClass(t.kind);
    box.append(h('span', { class: 'tok' + (t.kind === 'EOF' ? ' eof' : ''), title: `${t.kind} at ${t.line}:${t.col}`, onmouseenter: () => hoverSource(t.line, t.col, t.lexeme.length || 1), onmouseleave: clearHover, onclick: () => jumpTo(t.line, t.col) },
      h('span', { class: 't-' + c }, t.lexeme || '⟨eof⟩'), h('span', { class: 'k' }, t.kind)));
  }
  panel.append(box);
}

//===--- Trees ---------------------------------------------------------------===//

function cstColour(n) {
  if (n.label === 'ε') return { fill: '#12161c', stroke: '#2a313b', text: '#56616f', dash: '3 3' };
  if (n.terminal) return { fill: '#2a1d17', stroke: '#6b4334', text: '#f7b393' };
  if (n.label.includes("'") || n.label.endsWith('_tail') || n.label === 'return_value') return { fill: '#131c26', stroke: '#24405a' };
  return { fill: '#132433', stroke: '#1f5577' };
}
function astColour(n) {
  const l = n.label;
  if (l === 'ImplicitCast') return { fill: '#2a2412', stroke: '#8a6d1f', text: '#ffcb6b', dash: '4 3' };
  if (l.endsWith('Decl') || l === 'Program') return { fill: '#22182c', stroke: '#5d3f78', text: '#e2c6f5' };
  if (l.endsWith('Stmt') || l === 'Block') return { fill: '#131f33', stroke: '#2b4f80' };
  if (l.endsWith('Literal')) return { fill: '#2a1d17', stroke: '#6b4334', text: '#f7b393' };
  if (l === 'VarRef' || l === 'Assign') return { fill: '#12262a', stroke: '#1f6470' };
  return { fill: '#142619', stroke: '#2f6a3c' };
}

function treePanel(panel, root, opts) {
  const host = h('div', { class: 'tree-host' });
  const toolbar = h('div', { class: 'toolbar' }, ...opts.toolbar);
  panel.append(h('div', { class: 'tree-wrap' }, toolbar, host));
  const total = (function count(n) { return 1 + (n.children || []).reduce((s, c) => s + count(c), 0); })(root);
  const view = renderTree(host, root, {
    colour: opts.colour,
    collapseDepth: total > 260 ? opts.deepCollapse : Infinity,
    onHover: (n) => (n && n.line ? hoverSource(n.line, n.col, n.terminal ? n.detail?.length : undefined) : clearHover()),
  });
  toolbar.append(
    h('span', { style: 'white-space:nowrap' }, `${total} nodes`),
    h('button', { class: 'ghost', onclick: () => view.expandAll() }, 'Expand all'),
    h('button', { class: 'ghost', onclick: () => view.collapseTo(3) }, 'Collapse'),
    h('button', { class: 'ghost', onclick: () => view.fit() }, 'Fit'),
  );
}

function parsePanel(panel, r) {
  if (!r.parseTree) return notAvailable(panel, 'parse tree');
  const root = state.compressCst ? compressChains(r.parseTree) : r.parseTree;
  const failed = r.diagnostics?.some((d) => d.severity === 'error' && (d.stage === 'parser' || d.stage === 'lexer'));
  treePanel(panel, root, {
    colour: cstColour,
    deepCollapse: 3,
    toolbar: [
      h('label', {}, Object.assign(h('input', { type: 'checkbox', onchange: (e) => { state.compressCst = e.target.checked; renderPanel(); } }), { checked: state.compressCst }), 'Compress single-child chains'),
      h('span', { class: 'note', title: 'one node per grammar rule expanded' }, failed ? '⚠ partial tree: parsing stopped at the error' : 'Concrete parse tree: one node per rule of grammar/minic-ll1.bnf, tokens in orange, ε = empty production'),
    ],
  });
}

function astPanel(panel, r) {
  if (!r.ast) return notAvailable(panel, 'AST');
  const typed = !!r.symbols;
  treePanel(panel, r.ast, {
    colour: astColour,
    deepCollapse: 4,
    toolbar: [h('span', { class: 'note' }, typed
      ? 'Typed AST after semantic analysis. Dashed yellow nodes are implicit widening conversions (bool → int → float) inserted by sema.'
      : 'Untyped AST: semantic analysis failed (see error).')],
  });
}

//===--- Symbols -------------------------------------------------------------===//

function symbolsPanel(panel, r) {
  if (!r.symbols) return notAvailable(panel, 'symbol table');
  const pad = h('div', { class: 'panel-pad' });
  pad.append(h('p', { class: 'explain', html: 'The scoped symbol table built by <code>src/sema.cpp</code>. Scopes form a stack (<b>global › function › block@line:col</b>): lookup walks from the innermost scope outwards, so inner declarations <b>shadow</b> outer ones, while redeclaring a name in the <i>same</i> scope is an error. Function signatures are collected in a first pass, so calls may precede definitions (mutual recursion).' }));
  const table = h('table', { class: 'grid' }, h('tr', {}, ['Scope', 'Name', 'Kind', 'Type', 'Declared'].map((x) => h('th', {}, x))));
  for (const s of r.symbols)
    table.append(h('tr', { class: 'click', onmouseenter: () => hoverSource(s.line, s.col, s.name.length), onmouseleave: clearHover, onclick: () => jumpTo(s.line, s.col) },
      h('td', { style: `padding-left:${8 + (s.scope.split('›').length - 1) * 14}px;color:var(--muted)` }, s.scope),
      h('td', {}, s.name), h('td', {}, h('span', { class: 'kind ' + s.kind }, s.kind)), h('td', {}, s.type), h('td', {}, `${s.line}:${s.col}`)));
  pad.append(table);
  panel.append(pad);
}

//===--- Code views ------------------------------------------------------------===//

// Splits off a trailing comment *before* escaping, so the ';' inside "&lt;"
// can never be mistaken for the start of a comment.
function splitComment(line, marker) {
  let inStr = false;
  for (let i = 0; i < line.length; i++) {
    if (line[i] === '"') inStr = !inStr;
    else if (!inStr && line.startsWith(marker, i)) return [line.slice(0, i), line.slice(i)];
  }
  return [line, ''];
}
const commentSpan = (c) => (c ? `<span class="c-cm">${esc(c)}</span>` : '');

function hlIR(line) {
  const [code, comment] = splitComment(line, ';');
  return esc(code)
    .replace(/\b(define|declare|ret|br|label|alloca|load|store|call|tail|icmp|fcmp|phi|add|sub|mul|sdiv|srem|fadd|fsub|fmul|fdiv|frem|fneg|zext|sitofp|uitofp|select|xor|and|or|common|global|align|nsw|nuw|samesign|local_unnamed_addr|dso_local|to|eq|ne|slt|sle|sgt|sge|ult|ule|ugt|uge|olt|ole|ogt|oge|oeq|une|one|splat|insertelement|shufflevector|poison|zeroinitializer|unreachable|switch|attributes)\b/g, '<span class="c-kw">$1</span>')
    .replace(/\b(i1|i8|i32|i64|float|double|ptr|void)\b/g, '<span class="c-ty">$1</span>')
    .replace(/(@[\w.]+)/g, '<span class="c-glob">$1</span>')
    .replace(/(%[\w.]+)/g, '<span class="c-reg">$1</span>')
    .replace(/^([\w.]+):/, '<span class="c-lbl">$1</span>:') + commentSpan(comment);
}
function hlAsm(line) {
  let [code, comment] = splitComment(line, '//');
  if (!comment) [code, comment] = splitComment(line, ';');
  if (/^\s*\./.test(code)) return `<span class="c-dir">${esc(code)}</span>` + commentSpan(comment);
  if (/^[\w.$]+:/.test(code)) return esc(code).replace(/^([\w.$]+):/, '<span class="c-lbl">$1</span>:') + commentSpan(comment);
  return esc(code)
    .replace(/^(\s+)([a-z][\w.]*)/, '$1<span class="c-kw">$2</span>')
    .replace(/\b([wxsdqv]\d{1,2}|sp|wzr|xzr|lr|fp)\b/g, '<span class="c-reg">$1</span>')
    .replace(/(#-?[\w.]+)/g, '<span class="c-num">$1</span>')
    .replace(/\b(L[\w.]+|_\w+|\.L\w+)\b/g, '<span class="c-lbl">$1</span>') + commentSpan(comment);
}

// Maps each assembly line to the source line it came from, using the
// "// line N:" annotations the AArch64 backend emits.
function asmSourceMap(text) {
  let cur = null;
  return text.split('\n').map((l) => {
    const m = l.match(/^\s*\/\/ line (\d+):/);
    if (m) cur = Number(m[1]);
    else if (/^[A-Za-z_][\w$]*:/.test(l) && !/^L/.test(l)) cur = null; // new function
    return cur;
  });
}
const lineColour = (n) => `hsla(${(n * 67) % 360}, 70%, 55%, 0.09)`;

function codeView(text, { lang, title, sub, map, filter }) {
  const box = h('div');
  box.append(h('div', { class: 'code-head' }, h('b', {}, title), h('span', {}, sub || '')));
  const code = h('div', { class: 'code' });
  const hlFn = lang === 'ir' ? hlIR : hlAsm;
  const srcMap = map ? asmSourceMap(text) : null;
  let no = 0;
  text.trimEnd().split('\n').forEach((l, i) => {
    if (filter && filter(l)) return;
    no++;
    const row = h('div', { class: 'ln' }, h('span', { class: 'no' }, no), h('span', { class: 'tx', html: hlFn(l) || ' ' }));
    if (srcMap && srcMap[i]) {
      const s = srcMap[i];
      row.dataset.src = s;
      row.style.background = lineColour(s);
      row.addEventListener('mouseenter', () => { hoverSource(s); highlightAsmLine(s); });
      row.addEventListener('mouseleave', () => { clearHover(); highlightAsmLine(null); });
    }
    code.append(row);
  });
  box.append(code);
  return box;
}
function highlightAsmLine(line) {
  document.querySelectorAll('.ln.hot').forEach((r) => r.classList.remove('hot'));
  if (line == null) return;
  const rows = document.querySelectorAll(`.ln[data-src="${line}"]`);
  rows.forEach((r) => r.classList.add('hot'));
}

const irInstrs = (ir) => (ir || '').split('\n').filter((l) => /^\s+[%a-z]/.test(l) && !/^\s*;/.test(l)).length;

function irPanel(panel, r) {
  if (!r.ir) return notAvailable(panel, 'LLVM IR');
  panel.append(h('div', { class: 'toolbar' }, h('span', { html: 'Left: what mccomp emits (the coursework\'s <code>output.ll</code>): every variable is an <code>alloca</code> with a load/store per use, and <code>&amp;&amp;</code>/<code>||</code> become branches joined by a <code>phi</code>. Right: after LLVM\'s <b>-O2</b> pipeline: mem2reg/SROA promote allocas to SSA registers, then instcombine, loop and tail-call optimisations run.' })));
  const split = h('div', { class: 'split' });
  split.append(codeView(r.ir, { lang: 'ir', title: 'mccomp → LLVM IR (-O0)', sub: `${irInstrs(r.ir)} instructions` }));
  split.append(codeView(r.irOpt, { lang: 'ir', title: 'after opt -O2', sub: `${irInstrs(r.irOpt)} instructions` }));
  panel.append(split);
}

function asmPanel(panel, r) {
  if (!r.asm) return notAvailable(panel, 'assembly');
  const directive = (l) => state.hideDirectives && (/^\s+\.(cfi|loh|build_version|section|p2align|globl|type|size|ident|file|subsections)/.test(l) || /^\s*;\s*--/.test(l) || /^Lfunc_end/.test(l) || /^\s*;\s*%bb/.test(l) || l.trim() === '');
  panel.append(h('div', { class: 'toolbar' },
    h('label', {}, Object.assign(h('input', { type: 'checkbox', onchange: (e) => { state.hideDirectives = e.target.checked; renderPanel(); } }), { checked: state.hideDirectives }), 'Hide directives'),
    h('span', { html: `Target: <b>${r.target === 'darwin' ? 'Apple arm64 (Mach-O)' : 'Linux AArch64 (ELF)'}</b>. Colours link each block of mccomp's output to its source line; hover either side.` })));
  const split = h('div', { class: 'split' });
  split.append(codeView(r.asm, { lang: 'asm', title: 'mccomp AArch64 backend', sub: `${countInstrs(r.asm)} instructions`, map: true, filter: directive }));
  split.append(codeView(r.llvmAsm, { lang: 'asm', title: 'LLVM -O2 (llc)', sub: `${countInstrs(r.llvmAsm)} instructions`, filter: directive }));
  panel.append(split);
}

//===--- Run -----------------------------------------------------------------===//

async function runProgram() {
  selectTab('run');
  const btn = $('#run');
  btn.disabled = true;
  state.run = { pending: true };
  renderPanel();
  try {
    const res = await fetch('/api/run', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ source: src.value }) });
    state.run = await res.json();
  } catch (e) {
    state.run = { error: String(e) };
  }
  btn.disabled = false;
  if (state.tab === 'run') renderPanel();
}

function runPanel(panel) {
  const pad = h('div', { class: 'panel-pad' });
  pad.append(h('p', { class: 'explain', html: 'Builds the program <b>three independent ways</b> and runs each binary: LLVM IR via clang, the same IR after the -O2 pipeline, and mccomp\'s own AArch64 backend (native on Apple silicon). Identical output is the same differential check the test-suite runs. (Times include process start-up.) Programs need <code>int main()</code> and may use <code>print_int</code> / <code>print_float</code>.' }));
  const run = state.run;
  if (!run) pad.append(h('button', { class: 'primary', onclick: runProgram }, '▶ Build & run'));
  else if (run.pending) pad.append(h('div', { class: 'empty' }, 'Building and running…'));
  else if (run.error) pad.append(h('div', { class: 'verdict bad' }, run.error));
  else {
    const outs = run.results.filter((x) => !x.error).map((x) => x.stdout + '|' + x.exitCode);
    const agree = outs.length === run.results.length && outs.every((o) => o === outs[0]);
    pad.append(h('div', { class: 'verdict ' + (agree ? 'ok' : 'bad') }, agree ? `✓ All ${outs.length} backends agree` : '✗ Backends disagree'));
    const grid = h('div', { class: 'run-grid' });
    for (const x of run.results)
      grid.append(h('div', { class: 'card' }, h('h4', {}, x.label, h('span', {}, x.error ? 'build failed' : `exit ${x.exitCode} · ${x.ms.toFixed(1)} ms`)), h('pre', {}, x.error || x.stdout || '(no output)')));
    pad.append(grid, h('p', {}, h('button', { class: 'ghost', onclick: runProgram }, 'Run again')));
  }
  if (!state.info.nativeArm) pad.append(h('p', { class: 'explain' }, 'This machine is not AArch64, so only the LLVM pipelines run here.'));
  panel.append(pad);
}

//===--- Grammar -------------------------------------------------------------===//

const grammarCache = {};
async function grammarPanel(panel) {
  const pad = h('div', { class: 'panel-pad' });
  panel.append(pad);
  const pick = h('div', { class: 'seg', style: 'display:inline-flex;margin-bottom:12px' },
    ...[['minic-ll1.bnf', 'Transformed LL(1) grammar (implemented)'], ['minic.bnf', 'Original coursework grammar']].map(([f, label]) =>
      h('button', { class: state.grammar === f ? 'on' : '', onclick: () => { state.grammar = f; renderPanel(); } }, label)));
  pad.append(pick);
  const text = grammarCache[state.grammar] ??= await (await fetch('/grammar/' + state.grammar)).text();
  if (state.tab !== 'grammar') return;
  let g, a;
  try {
    g = parseGrammar(text);
    a = analyse(g);
  } catch (e) {
    return pad.append(h('div', { class: 'verdict bad' }, String(e.message)));
  }
  const ll1 = state.grammar === 'minic-ll1.bnf';
  pad.append(h('p', { class: 'explain', html: ll1
    ? 'The grammar the parser implements, analysed live in your browser by <code>ll1.mjs</code>. Left recursion was eliminated (<code>A ::= A α | β</code> → <code>A ::= β A\'</code>, <code>A\' ::= α A\' | ε</code>), precedence encoded as one non-terminal per level, and common prefixes left-factored. The single remaining conflict, <code>M[expr, IDENT]</code>, is resolved in the parser by peeking at a second token: <code>IDENT "="</code> means assignment.'
    : 'The grammar exactly as given in the coursework. A top-down parser cannot use it directly: the <b>left-recursive</b> rules would recurse forever, and <code>rval</code> is <b>ambiguous</b> (no precedence or associativity), which shows up as many LL(1) table conflicts.' }));
  const altText = (A, i) => { const alt = g.rules.get(A).alts[i]; return alt.length ? alt.map(display).join(' ') : 'ε'; };
  pad.append(h('div', { class: 'chips' },
    h('span', { class: 'chip' }, h('b', {}, g.nonterminals.length), 'non-terminals'),
    h('span', { class: 'chip' }, h('b', {}, g.terminals.length), 'terminals'),
    h('span', { class: 'chip ' + (a.leftRecursion.length ? 'bad' : 'good') }, h('b', {}, a.leftRecursion.length), 'left-recursive cycles'),
    h('span', { class: 'chip ' + (a.conflicts.length > 1 ? 'bad' : 'good') }, h('b', {}, a.conflicts.length), 'LL(1) conflicts')));

  if (a.leftRecursion.length) {
    pad.append(h('h3', {}, 'Left recursion'));
    pad.append(h('ul', { class: 'plain' }, a.leftRecursion.map((c) => h('li', {}, c.join(' ⇒ ') + ' …'))));
  }
  if (a.conflicts.length) {
    pad.append(h('h3', {}, 'LL(1) conflicts'));
    pad.append(h('ul', { class: 'plain' }, a.conflicts.slice(0, 60).map((c) => h('li', {}, `M[${c.nonterminal}, ${display(c.terminal)}] = `, c.alternatives.map((i) => altText(c.nonterminal, i)).join('   |   ')))));
  }

  pad.append(h('h3', {}, 'Nullable, FIRST and FOLLOW sets'));
  const set = (s) => [...s].map(display).sort().join('  ');
  const ff = h('table', { class: 'grid' }, h('tr', {}, ['Non-terminal', 'Nullable', 'FIRST', 'FOLLOW'].map((x) => h('th', {}, x))));
  for (const n of g.nonterminals) ff.append(h('tr', {}, h('td', {}, n), h('td', {}, a.nullable.has(n) ? 'ε' : ''), h('td', {}, set(a.first.get(n))), h('td', {}, set(a.follow.get(n)))));
  pad.append(h('div', { class: 'scroll-x' }, ff));

  pad.append(h('h3', {}, 'Predictive parse table M[non-terminal, lookahead]'));
  const terms = [...g.terminals, EOF];
  const pt = h('table', { class: 'grid ptable' }, h('tr', {}, h('th', {}, ''), terms.map((t) => h('th', {}, display(t)))));
  for (const n of g.nonterminals) {
    const row = a.table.get(n);
    pt.append(h('tr', {}, h('th', {}, n), terms.map((t) => {
      const alts = row.get(t);
      if (!alts) return h('td', {}, '');
      const label = alts.map((i) => altText(n, i)).join(' | ');
      return h('td', { class: alts.length > 1 ? 'conflict' : 'entry', title: `${n} → ${label}` }, label);
    })));
  }
  pad.append(h('div', { class: 'scroll-x' }, pt));
}

//===--- Boot ----------------------------------------------------------------===//

function setTarget(t) {
  state.target = t;
  localStorage.setItem('mccomp.target', t);
  document.querySelectorAll('#target button').forEach((b) => b.classList.toggle('on', b.dataset.target === t));
  compile();
}
document.querySelectorAll('#target button').forEach((b) => b.addEventListener('click', () => setTarget(b.dataset.target)));
$('#run').addEventListener('click', runProgram);

async function boot() {
  document.querySelectorAll('#target button').forEach((b) => b.classList.toggle('on', b.dataset.target === state.target));
  state.info = await (await fetch('/api/info')).json();
  const examples = await (await fetch('/api/examples')).json();
  const sel = $('#examples');
  const groups = {};
  for (const ex of examples) {
    const dir = ex.name.split('/').slice(0, -1).join('/');
    (groups[dir] ??= h('optgroup', { label: dir })).append(h('option', { value: ex.name }, ex.name.split('/').pop()));
  }
  Object.values(groups).forEach((g) => sel.append(g));
  const byName = Object.fromEntries(examples.map((e) => [e.name, e.source]));
  sel.addEventListener('change', () => {
    src.value = byName[sel.value];
    localStorage.setItem('mccomp.example', sel.value);
    renderEditor();
    compile();
  });
  const saved = localStorage.getItem('mccomp.example');
  sel.value = byName[saved] ? saved : 'examples/showcase.c' in byName ? 'examples/showcase.c' : examples[0].name;
  src.value = byName[sel.value];
  renderEditor();
  renderTabs();
  compile();
}
boot();
