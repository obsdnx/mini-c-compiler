// SVG tree view with tidy layout, collapsing, pan and zoom. No dependencies.
const SVG = 'http://www.w3.org/2000/svg';
const LEVEL_H = 62;
const NODE_H = 34;
const GAP = 14;

const el = (name, attrs = {}, parent) => {
  const e = document.createElementNS(SVG, name);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  if (parent) parent.appendChild(e);
  return e;
};

// Collapse chains of single-child non-terminals (expr › or_expr › … › primary)
// into one node: this is exactly the noise an AST removes.
export function compressChains(node) {
  const chain = [node];
  let cur = node;
  while (cur.children && cur.children.length === 1 && !cur.children[0].terminal && cur.children[0].children) {
    cur = cur.children[0];
    chain.push(cur);
  }
  const children = (cur.children || []).map(compressChains);
  if (chain.length === 1) return { ...node, children };
  return {
    ...cur,
    label: `${chain[0].label} ⋯ ${cur.label}`,
    detail: `${chain.length} rules`,
    chain: chain.map((c) => c.label),
    line: chain[0].line,
    col: chain[0].col,
    children,
  };
}

function measure(n) {
  const text = n.label.length * 7 + (n.detail ? n.detail.length * 6.5 + 8 : 0);
  return Math.min(Math.max(text + 20, 34), 300);
}

// Classic recursive layout: each subtree occupies max(own width, sum of child
// subtree widths) and a parent is centred over its children.
function layout(n, depth) {
  n.w = measure(n.data);
  n.depth = depth;
  const kids = n.collapsed ? [] : n.kids;
  if (!kids.length) {
    n.span = n.w;
    return;
  }
  kids.forEach((k) => layout(k, depth + 1));
  const sum = kids.reduce((s, k) => s + k.span, 0) + GAP * (kids.length - 1);
  n.span = Math.max(n.w, sum);
}
function place(n, left) {
  n.y = n.depth * LEVEL_H;
  const kids = n.collapsed ? [] : n.kids;
  if (!kids.length) {
    n.x = left + n.span / 2;
    return;
  }
  const sum = kids.reduce((s, k) => s + k.span, 0) + GAP * (kids.length - 1);
  let x = left + (n.span - sum) / 2;
  for (const k of kids) {
    place(k, x);
    x += k.span + GAP;
  }
  n.x = (kids[0].x + kids[kids.length - 1].x) / 2;
}

function wrap(data, depth, collapseDepth) {
  const n = { data, kids: [], collapsed: false };
  n.kids = (data.children || []).map((c) => wrap(c, depth + 1, collapseDepth));
  n.collapsed = n.kids.length > 0 && depth >= collapseDepth;
  return n;
}

function countNodes(n) {
  return 1 + n.kids.reduce((s, k) => s + countNodes(k), 0);
}

// Renders `root` into `host`. options: { colour(node) -> {fill, stroke}, onHover(node|null), collapseDepth }
export function renderTree(host, root, options = {}) {
  host.innerHTML = '';
  const tree = wrap(root, 0, options.collapseDepth ?? Infinity);
  const svg = el('svg', {}, host);
  const view = el('g', {}, svg);
  let scale = 1, tx = 0, ty = 0;
  const apply = () => view.setAttribute('transform', `translate(${tx},${ty}) scale(${scale})`);

  function draw(fit, tight = false) {
    view.innerHTML = '';
    layout(tree, 0);
    place(tree, 0);
    const edges = el('g', {}, view);
    const nodes = el('g', {}, view);
    let maxDepth = 0;
    (function walk(n) {
      maxDepth = Math.max(maxDepth, n.depth);
      const kids = n.collapsed ? [] : n.kids;
      for (const k of kids) {
        const y1 = n.y + NODE_H, y2 = k.y, my = (y1 + y2) / 2;
        el('path', { class: 'edge', d: `M${n.x},${y1} C${n.x},${my} ${k.x},${my} ${k.x},${y2}` }, edges);
        walk(k);
      }
      const d = n.data;
      const c = options.colour ? options.colour(d) : { fill: '#161c24', stroke: '#2c3643' };
      const g = el('g', { class: 'node' + (n.kids.length ? ' collapsible' : ''), transform: `translate(${n.x - n.w / 2},${n.y})` }, nodes);
      el('rect', { width: n.w, height: NODE_H, fill: c.fill, stroke: c.stroke, 'stroke-dasharray': c.dash || '' }, g);
      const t = el('text', { x: 10, y: d.detail ? 14 : 21 }, g);
      t.textContent = d.label;
      if (c.text) t.setAttribute('fill', c.text);
      if (d.detail) {
        const dt = el('text', { x: 10, y: 27, class: 'detail' }, g);
        dt.textContent = d.detail.length > 42 ? d.detail.slice(0, 41) + '…' : d.detail;
      }
      if (n.collapsed) {
        const b = el('text', { x: n.w - 8, y: 12, class: 'badge', 'text-anchor': 'end' }, g);
        b.textContent = `+${countNodes(n) - 1}`;
      }
      const title = el('title', {}, g);
      title.textContent = (d.chain ? d.chain.join(' → ') : d.label + (d.detail ? ' ' + d.detail : '')) +
        (d.line ? `\n${d.line}:${d.col}` : '') + (n.kids.length ? '\nclick to ' + (n.collapsed ? 'expand' : 'collapse') : '');
      g.addEventListener('mouseenter', () => options.onHover && options.onHover(d));
      g.addEventListener('mouseleave', () => options.onHover && options.onHover(null));
      g.addEventListener('click', (e) => {
        e.stopPropagation();
        if (!n.kids.length) return;
        n.collapsed = !n.collapsed;
        draw(false);
      });
    })(tree);
    if (fit) fitView(maxDepth, tight);
    apply();
  }

  function fitView(maxDepth, tight) {
    const w = host.clientWidth || 800, h = host.clientHeight || 600;
    const treeW = tree.span, treeH = (maxDepth + 1) * LEVEL_H;
    // Shrink to fit, but never below a readable size; pan to see the rest.
    scale = Math.max(tight ? 0.15 : 0.8, Math.min(1.1, (w - 40) / treeW, (h - 40) / treeH));
    tx = treeW * scale > w ? w / 2 - tree.x * scale : (w - treeW * scale) / 2;
    ty = 20;
  }

  // Pan with drag, zoom with wheel around the cursor.
  let drag = null;
  svg.addEventListener('mousedown', (e) => {
    drag = { x: e.clientX - tx, y: e.clientY - ty };
    host.classList.add('dragging');
  });
  window.addEventListener('mouseup', () => {
    drag = null;
    host.classList.remove('dragging');
  });
  svg.addEventListener('mousemove', (e) => {
    if (!drag) return;
    tx = e.clientX - drag.x;
    ty = e.clientY - drag.y;
    apply();
  });
  svg.addEventListener('wheel', (e) => {
    e.preventDefault();
    const r = svg.getBoundingClientRect();
    const mx = e.clientX - r.left, my = e.clientY - r.top;
    const k = Math.exp(-e.deltaY * 0.0015);
    const ns = Math.min(3, Math.max(0.15, scale * k));
    tx = mx - ((mx - tx) * ns) / scale;
    ty = my - ((my - ty) * ns) / scale;
    scale = ns;
    apply();
  }, { passive: false });

  draw(true);
  return {
    expandAll() {
      (function f(n) { n.collapsed = false; n.kids.forEach(f); })(tree);
      draw(true);
    },
    collapseTo(depth) {
      (function f(n, d) { n.collapsed = n.kids.length > 0 && d >= depth; n.kids.forEach((k) => f(k, d + 1)); })(tree, 0);
      draw(true);
    },
    fit() { draw(true, true); },
    size: countNodes(tree),
  };
}
