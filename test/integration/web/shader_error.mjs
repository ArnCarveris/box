#!/usr/bin/env node
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { extname, join, resolve } from 'node:path';

const USAGE = `Checks the shader error path of the browser build in headless Chrome.

  node test/integration/web/shader_error.mjs [--chrome <path>] [--out <dir>] <bin dir>

<bin dir> holds boxrun.html, .js and .wasm, e.g.
.bake/local_env/<env>/wasm32-Emscripten/debug/bin, or under <output_dir>/local_env when
bake3's bake.json sets output_dir; "$(tools/integration.py local-env --env <env>)" prints the
local env folder either way. The page is served on a free localhost
port and opened with etc/scenes/default.flecs; the check breaks /etc/shaders/mesh.wgsl in the
page's file system, then restores it. It passes when the break logs one compile error with its
line, no WebGPU object turns invalid and the scene keeps rendering. Exits 0 on a pass, 1 on a
failure and 2 when Chrome or the page cannot start.`;

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
const CHROME_DEFAULT = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function parse(argv) {
  const opts = { chrome: process.env.CHROME || CHROME_DEFAULT, out: null, dir: null };
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--chrome') opts.chrome = argv[++i];
    else if (argv[i] === '--out') opts.out = argv[++i];
    else if (argv[i] === '-h' || argv[i] === '--help') { console.log(USAGE); process.exit(0); }
    else opts.dir = resolve(argv[i]);
  }
  if (!opts.dir || !existsSync(join(opts.dir, 'boxrun.html'))) {
    console.error(USAGE);
    process.exit(2);
  }
  return opts;
}

function serve(dir) {
  const server = createServer((req, res) => {
    const file = join(dir, decodeURIComponent(new URL(req.url, 'http://x').pathname));
    if (!file.startsWith(dir) || !existsSync(file)) { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { 'Content-Type': MIME[extname(file)] || 'application/octet-stream' });
    res.end(readFileSync(file));
  });
  return new Promise((r) => server.listen(0, '127.0.0.1', () => r(server)));
}

async function connect(port) {
  for (let i = 0; i < 100; i++) {
    try {
      const targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
      const page = targets.find((t) => t.type === 'page');
      if (page) return page.webSocketDebuggerUrl;
    } catch { }
    await sleep(100);
  }
  return null;
}

async function main() {
  const opts = parse(process.argv.slice(2));
  const server = await serve(opts.dir);
  const profile = mkdtempSync(join(tmpdir(), 'box-web-'));
  const debugPort = 20000 + Math.floor(Math.random() * 20000);
  const chrome = spawn(opts.chrome, ['--headless=new', '--enable-unsafe-webgpu',
    `--remote-debugging-port=${debugPort}`, `--user-data-dir=${profile}`, '--no-first-run',
    '--window-size=640,480', 'about:blank'], { stdio: 'ignore' });
  let status = 2;
  const log = [];
  try {
    const url = await connect(debugPort);
    if (!url) throw new Error('chrome did not start');
    const ws = new WebSocket(url);
    await new Promise((r) => ws.addEventListener('open', r));
    let id = 0;
    const pending = new Map();
    ws.addEventListener('message', (ev) => {
      const msg = JSON.parse(ev.data);
      if (msg.id && pending.has(msg.id)) { pending.get(msg.id)(msg); pending.delete(msg.id); }
      else if (msg.method === 'Runtime.consoleAPICalled') {
        log.push(msg.params.args.map((a) => a.value ?? a.description).join(' ')
          .replace(/\x1b\[[0-9;]*m/g, ''));
      } else if (msg.method === 'Log.entryAdded') {
        log.push(`[browser] ${msg.params.entry.text}`);
      }
    });
    const send = (method, params = {}) => new Promise((r) => {
      const n = ++id; pending.set(n, r); ws.send(JSON.stringify({ id: n, method, params }));
    });
    const evaluate = async (expression) => (await send('Runtime.evaluate',
      { expression, awaitPromise: true, returnByValue: true })).result?.result?.value;
    const shot = async (name) => {
      const r = await send('Page.captureScreenshot', { format: 'png' });
      if (opts.out) writeFileSync(join(opts.out, name), Buffer.from(r.result.data, 'base64'));
      return r.result.data;
    };
    await send('Runtime.enable');
    await send('Log.enable');
    await send('Page.enable');
    const port = server.address().port;
    await send('Page.navigate', { url: `http://127.0.0.1:${port}/boxrun.html#etc/scenes/default.flecs` });
    for (let i = 0; i < 100 && !(await evaluate('!!(window.Module && Module.FS)')); i++) {
      await sleep(200);
    }
    if (!(await evaluate('!!(window.Module && Module.FS)'))) throw new Error('page did not start');
    await sleep(2000);
    await shot('loaded.png');
    const lines = await evaluate(`window.orig = Module.FS.readFile('/etc/shaders/mesh.wgsl',
      {encoding: 'utf8'}); window.orig.split('\\n').length`);
    const start = log.length;
    await evaluate(`Module.FS.writeFile('/etc/shaders/mesh.wgsl', window.orig +
      '\\nfn broken() -> f32 { return undefined_name; }\\n'); 1`);
    await sleep(3000);
    const broken = await shot('broken.png');
    const during = log.slice(start);
    await evaluate("Module.FS.writeFile('/etc/shaders/mesh.wgsl', window.orig); 1");
    await sleep(3000);
    await shot('restored.png');
    const after = log.slice(start + during.length);
    const errors = during.filter((l) => /failed to compile: .*wgsl:\d+: error: .*undefined_name/.test(l));
    const invalid = log.filter((l) => /Invalid (ShaderModule|RenderPipeline|CommandBuffer)/.test(l));
    const relapse = after.filter((l) => /failed to compile/.test(l));
    console.log(`mesh.wgsl: ${lines} lines, broken at line ${lines + 1}`);
    console.log(`compile errors while broken: ${errors.length}`);
    console.log(`invalid WebGPU objects: ${invalid.length}`);
    console.log(`compile errors after restoring: ${relapse.length}`);
    status = errors.length === 1 && !invalid.length && !relapse.length && broken.length ? 0 : 1;
    if (status) console.log(log.join('\n'));
    ws.close();
  } catch (err) {
    console.error(String(err && err.message ? err.message : err));
  } finally {
    chrome.kill('SIGTERM');
    server.close();
    await sleep(300);
    rmSync(profile, { recursive: true, force: true });
  }
  console.log(status === 0 ? 'PASS' : 'FAIL');
  process.exit(status);
}

main();
