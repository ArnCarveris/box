#!/usr/bin/env node
import { spawn, spawnSync } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdtempSync, readFileSync, realpathSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const USAGE = `Checks TextureReadback in the browser build in headless Chrome: the images of texture
graphs are mapped back to the CPU without waiting and kept as copies.

  node test/integration/web/texture_readback.mjs [--chrome <path>] <bin dir>

<bin dir> holds boxrun.html, .js and .wasm, e.g.
.bake/local_env/<env>/wasm32-Emscripten/debug/bin, or under <output_dir>/local_env when
bake3's bake.json sets output_dir; "$(tools/integration.py local-env --env <env>)" prints the
local env folder either way. The page is served on a free localhost port and opened with
test/integration/web/texture_readback.flecs, which the page preloads from the same server
(?preload=), at a fixed delta of 1/60 s: a GraphColor, a GraphFloat and a GraphHdr graph with a
TextureReadback. The check reads their box.texture.ReadbackStatus through the shell of the page
until all three are ready with the format and size of their copy, and passes when they are and
the log has no error and no invalid WebGPU object. Exits 0 on a pass, 1 on a failure and 2 when
Chrome or the page cannot start.`;

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const SCENE = '/test/integration/web/texture_readback.flecs';
// Files the page preloads: the scene
const PRELOAD = [SCENE];
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
const CHROME_DEFAULT = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const DELTA_TIME = '0.016666667';
const TIMEOUT_MS = 90000;
const POLL_MS = 250;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// The graphs of the scene and the format and width of their copies
const COPIES = [
  { name: 'colors', format: 'ReadbackRGBA8', width: 64 },
  { name: 'heights', format: 'ReadbackR32Float', width: 64 },
  { name: 'hdr', format: 'ReadbackRGBA32Float', width: 32 }
];

// Runs a command line in the shell of the page and returns its output
const shell = (line) => `Module.cwrap('box_command_web_run', 'string', ['string'])(` +
  `${JSON.stringify(line)})`;

function parse(argv) {
  const opts = { chrome: process.env.CHROME || CHROME_DEFAULT, dir: null };
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--chrome') opts.chrome = argv[++i];
    else if (argv[i] === '-h' || argv[i] === '--help') { console.log(USAGE); process.exit(0); }
    else opts.dir = resolve(argv[i]);
  }
  if (!opts.dir || !existsSync(join(opts.dir, 'boxrun.html'))) {
    console.error(USAGE);
    process.exit(2);
  }
  return opts;
}

// macOS holds a quarantined app at its first launch check, which a headless session never
// passes, so fail at once instead of "chrome did not start" after the connect timeout. Approving
// the app in the Finder keeps the attribute but sets the user-approved flag 0x0040, which passes.
function checkQuarantine(chrome) {
  if (process.platform !== 'darwin') return;
  let app = existsSync(chrome) ? realpathSync(chrome) : resolve(chrome);
  while (!app.endsWith('.app')) {
    if (dirname(app) === app) return;
    app = dirname(app);
  }
  const xattr = spawnSync('xattr', ['-p', 'com.apple.quarantine', app], { encoding: 'utf8' });
  if (xattr.error || xattr.status !== 0) return;
  if (parseInt(xattr.stdout.split(';')[0], 16) & 0x0040) return;
  console.error(`FAIL: ${app} has the com.apple.quarantine attribute, so macOS holds Chrome at ` +
    'its first launch check. Open Chrome once from the Finder and confirm it, or run\n' +
    `  xattr -dr com.apple.quarantine "${app}"\n` +
    'or pass --chrome with a copy without the attribute.');
  process.exit(2);
}

// Serves the page from the bin dir and the scene from the repository
function serve(dir) {
  const server = createServer((req, res) => {
    const path = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    const ours = PRELOAD.includes(path);
    const file = ours ? join(ROOT, path) : join(dir, path);
    if ((!ours && !file.startsWith(dir)) || !existsSync(file)) {
      res.writeHead(404); res.end(); return;
    }
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

// Whether the status of a copy is ready with its format and width
function done(copy, status) {
  const value = (key) => (status.match(new RegExp(`"${key}":\\s*"?(\\w+)`)) || [])[1];
  return value('ready') === 'true' && value('format') === copy.format &&
    Number(value('width')) === copy.width && Number(value('revision')) > 0;
}

async function main() {
  const opts = parse(process.argv.slice(2));
  checkQuarantine(opts.chrome);
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
    await send('Runtime.enable');
    await send('Log.enable');
    await send('Page.enable');
    await send('Emulation.setDeviceMetricsOverride',
      { width: 640, height: 480, deviceScaleFactor: 1, mobile: false });
    const port = server.address().port;
    const hash = encodeURIComponent(`--delta-time ${DELTA_TIME} ${SCENE}`);
    const preload = PRELOAD.map((f) => `preload=${encodeURIComponent(f)}`).join('&');
    await send('Page.navigate',
      { url: `http://127.0.0.1:${port}/boxrun.html?${preload}#${hash}` });
    for (let i = 0; i < 100 && !(await evaluate('!!(window.Module && Module.FS)')); i++) {
      await sleep(200);
    }
    if (!(await evaluate('!!(window.Module && Module.FS)'))) throw new Error('page did not start');

    // The copies are ready once the graphs are evaluated and their images mapped
    const deadline = Date.now() + TIMEOUT_MS;
    const states = {};
    const ready = () => COPIES.every((c) => done(c, states[c.name] || ''));
    while (!ready() && Date.now() < deadline) {
      for (const c of COPIES) {
        states[c.name] = String(await evaluate(shell(`get ${c.name} box.texture.ReadbackStatus`)));
      }
      if (!ready()) await sleep(POLL_MS);
    }
    const errors = log.filter((l) => /error/i.test(l));
    const invalid = log.filter((l) =>
      /Invalid (ShaderModule|RenderPipeline|CommandBuffer|Buffer|BindGroup|Texture)/.test(l));
    for (const c of COPIES) console.log(`${c.name}: ${states[c.name].replace(/\s+/g, ' ')}`);
    console.log(`errors: ${errors.length}, invalid WebGPU objects: ${invalid.length}`);
    status = ready() && !errors.length && !invalid.length ? 0 : 1;
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
