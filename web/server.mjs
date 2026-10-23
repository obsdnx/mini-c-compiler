// Local server for the Mini-C compiler explorer. Zero dependencies.
//   node web/server.mjs            then open http://localhost:5325
//
// POST /api/compile  {source, target}  -> every compiler stage as JSON (mccomp --json)
// POST /api/run      {source}          -> builds and runs the program three ways
// GET  /api/examples                   -> bundled example programs
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { mkdtemp, readdir, readFile, rm, writeFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { dirname, extname, join, normalize } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const PUBLIC = join(ROOT, 'web', 'public');
const MCC = join(ROOT, 'build', 'mccomp');
const RUNTIME = join(ROOT, 'runtime', 'runtime.c');
const PORT = Number(process.env.PORT || 5325);
const NATIVE_ARM = process.arch === 'arm64';
const RUNTIME_FUNCTIONS = new Set(['print_int', 'print_float']);

function findClang() {
  const candidates = ['/opt/homebrew/opt/llvm/bin/clang', '/usr/local/opt/llvm/bin/clang', '/usr/lib/llvm-23/bin/clang'];
  return candidates.find(existsSync) || 'clang';
}
const CLANG = findClang();

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css', '.svg': 'image/svg+xml' };

function run(cmd, args, { input, timeoutMs = 5000 } = {}) {
  return new Promise((resolve) => {
    const start = process.hrtime.bigint();
    const child = spawn(cmd, args, { stdio: ['pipe', 'pipe', 'pipe'] });
    let stdout = '', stderr = '', killed = false;
    const timer = setTimeout(() => {
      killed = true;
      child.kill('SIGKILL');
    }, timeoutMs);
    child.stdout.on('data', (d) => { if (stdout.length < 1e6) stdout += d; });
    child.stderr.on('data', (d) => { if (stderr.length < 1e5) stderr += d; });
    child.on('error', (e) => { clearTimeout(timer); resolve({ code: -1, stdout, stderr: String(e), ms: 0 }); });
    child.on('close', (code, signal) => {
      clearTimeout(timer);
      const ms = Number(process.hrtime.bigint() - start) / 1e6;
      resolve({ code, signal, stdout, stderr: killed ? `killed after ${timeoutMs} ms` : stderr, ms, timedOut: killed });
    });
    child.stdin.end(input ?? '');
  });
}

async function compile(source, target) {
  const args = ['-', '--json'];
  if (target === 'darwin' || target === 'linux') args.push(`--target=${target}`);
  const r = await run(MCC, args, { input: source });
  if (r.code !== 0) throw new Error(r.stderr || 'mccomp failed');
  return JSON.parse(r.stdout);
}

async function runProgram(source) {
  const info = await compile(source, '');
  if (!info.ok) return { error: 'The program has compile errors.' };
  const bad = (info.symbols || []).filter((s) => s.kind === 'extern' && !RUNTIME_FUNCTIONS.has(s.name));
  if (bad.length)
    return { error: `For safety, the runner only links ${[...RUNTIME_FUNCTIONS].join(' and ')}; this program declares extern ${bad.map((s) => s.name).join(', ')}.` };
  if (!(info.symbols || []).some((s) => s.name === 'main' && s.kind === 'function'))
    return { error: 'Define `int main()` to run the program.' };

  const dir = await mkdtemp(join(tmpdir(), 'mccomp-'));
  try {
    const src = join(dir, 'prog.c');
    await writeFile(src, source);
    const pipelines = [
      { id: 'llvm-O0', label: 'LLVM IR → clang -O0', emit: 'ir', ext: 'll' },
      { id: 'llvm-O2', label: 'LLVM IR (O2 pipeline) → clang', emit: 'ir-opt', ext: 'll' },
    ];
    if (NATIVE_ARM) pipelines.push({ id: 'aarch64', label: 'mccomp AArch64 backend', emit: 'asm', ext: 's' });
    const results = [];
    for (const p of pipelines) {
      const out = join(dir, `${p.id}.${p.ext}`);
      const exe = join(dir, p.id);
      const c1 = await run(MCC, [src, `--emit=${p.emit}`, '-o', out]);
      const c2 = c1.code === 0 ? await run(CLANG, ['-w', out, RUNTIME, '-lm', '-o', exe], { timeoutMs: 20000 }) : c1;
      if (c2.code !== 0) {
        results.push({ ...p, error: c2.stderr });
        continue;
      }
      const r = await run(exe, [], { timeoutMs: 3000 });
      results.push({ ...p, stdout: r.stdout, exitCode: r.code, ms: r.ms, timedOut: r.timedOut, stderr: r.stderr });
    }
    return { results };
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
}

async function examples() {
  const out = [];
  for (const dir of ['examples', 'tests/programs', 'bench']) {
    const files = (await readdir(join(ROOT, dir)).catch(() => [])).filter((f) => f.endsWith('.c')).sort();
    for (const f of files) out.push({ name: `${dir}/${f}`, source: await readFile(join(ROOT, dir, f), 'utf8') });
  }
  for (const f of (await readdir(join(ROOT, 'tests/errors'))).filter((f) => f.endsWith('.c')).sort().slice(0, 12))
    out.push({ name: `tests/errors/${f}`, source: await readFile(join(ROOT, 'tests/errors', f), 'utf8') });
  return out;
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let body = '';
    req.on('data', (d) => {
      body += d;
      if (body.length > 200_000) reject(new Error('request too large'));
    });
    req.on('end', () => resolve(body));
  });
}

const json = (res, status, obj) => {
  res.writeHead(status, { 'content-type': 'application/json' });
  res.end(JSON.stringify(obj));
};

const server = createServer(async (req, res) => {
  try {
    const url = new URL(req.url, 'http://localhost');
    if (req.method === 'POST' && (url.pathname === '/api/compile' || url.pathname === '/api/run')) {
      const { source = '', target = '' } = JSON.parse(await readBody(req));
      return json(res, 200, url.pathname === '/api/compile' ? await compile(source, target) : await runProgram(source));
    }
    if (url.pathname === '/api/examples') return json(res, 200, await examples());
    if (url.pathname === '/api/info') return json(res, 200, { nativeArm: NATIVE_ARM, platform: process.platform });
    if (url.pathname.startsWith('/grammar/') && url.pathname.endsWith('.bnf')) {
      const file = normalize(join(ROOT, url.pathname));
      if (!file.startsWith(join(ROOT, 'grammar'))) return json(res, 404, {});
      res.writeHead(200, { 'content-type': 'text/plain; charset=utf-8' });
      return res.end(await readFile(file));
    }
    const file = normalize(join(PUBLIC, url.pathname === '/' ? 'index.html' : url.pathname));
    if (!file.startsWith(PUBLIC) || !existsSync(file)) return json(res, 404, { error: 'not found' });
    res.writeHead(200, { 'content-type': (MIME[extname(file)] || 'application/octet-stream') + '; charset=utf-8' });
    res.end(await readFile(file));
  } catch (e) {
    json(res, 500, { error: String(e.message || e) });
  }
});

if (!existsSync(MCC)) {
  console.error('build/mccomp not found: run `make` first.');
  process.exit(1);
}
// Bound to loopback only: /api/run executes the submitted program.
server.listen(PORT, '127.0.0.1', () => console.log(`Mini-C explorer: http://localhost:${PORT}`));
