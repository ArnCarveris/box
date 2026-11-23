#!/usr/bin/env node
import { spawn, spawnSync } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdtempSync, readFileSync, realpathSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { inflateSync } from 'node:zlib';

const USAGE = `Checks Terrain in the browser build in headless Chrome: the height graphs of the
tiles evaluated into r32float images, copied into the storage buffer of the grid mesh and read by
the vertex stages of the color, depth and shadow passes.

  node test/integration/web/terrain.mjs [--chrome <path>] [--out <dir>] <bin dir>

<bin dir> holds boxrun.html, .js and .wasm, e.g.
.bake/local_env/<env>/wasm32-Emscripten/debug/bin, or under <output_dir>/local_env when
bake3's bake.json sets output_dir; "$(tools/integration.py local-env --env <env>)" prints the
local env folder either way. The page is served on a free localhost port and opened with
test/integration/web/terrain.flecs, which the page preloads from the same server (?preload=), at
a fixed delta of 1/60 s. The check takes screenshots until the terrain covers the image without
a magenta pixel of the clear and its green varies with light and shade, and passes when it does
and the log has no error and no invalid WebGPU object. Exits 0 on a pass, 1 on a failure and 2
when Chrome or the page cannot start.`;

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const SCENE = '/test/integration/web/terrain.flecs';
// Files the page preloads: the scene
const PRELOAD = [SCENE];
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
const CHROME_DEFAULT = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const DELTA_TIME = '0.016666667';
const TIMEOUT_MS = 90000;
const SHOT_MS = 250;
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

// Decodes an 8 bit RGB or RGBA PNG, as Chrome's screenshots are, into rows of RGB
function decodePng(buf) {
  let pos = 8, width = 0, height = 0, channels = 0;
  const idat = [];
  while (pos < buf.length) {
    const length = buf.readUInt32BE(pos);
    const type = buf.toString('latin1', pos + 4, pos + 8);
    const body = buf.subarray(pos + 8, pos + 8 + length);
    if (type === 'IHDR') {
      width = body.readUInt32BE(0);
      height = body.readUInt32BE(4);
      if (body[8] !== 8 || (body[9] !== 2 && body[9] !== 6)) throw new Error('unsupported PNG');
      channels = body[9] === 6 ? 4 : 3;
    } else if (type === 'IDAT') idat.push(body);
    pos += 12 + length;
  }
  const data = inflateSync(Buffer.concat(idat));
  const stride = width * channels;
  const rows = [];
  let prev = new Uint8Array(stride);
  for (let y = 0; y < height; y++) {
    const filter = data[y * (stride + 1)];
    const line = new Uint8Array(data.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1)));
    for (let i = 0; i < stride; i++) {
      const a = i >= channels ? line[i - channels] : 0;
      const b = prev[i];
      const c = i >= channels ? prev[i - channels] : 0;
      let add = 0;
      if (filter === 1) add = a;
      else if (filter === 2) add = b;
      else if (filter === 3) add = (a + b) >> 1;
      else if (filter === 4) {
        const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
        add = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
      }
      line[i] = (line[i] + add) & 255;
    }
    rows.push(line);
    prev = line;
  }
  return { width, height, pixel: (x, y) => {
    const row = rows[y], i = x * channels;
    return [row[i], row[i + 1], row[i + 2]];
  } };
}

// The terrain fills the image: no pixel is the magenta clear, and the green of the lit and the
// shaded slopes spans a range
function measure(image) {
  let magenta = 0, lo = 255, hi = 0;
  for (let y = 0; y < image.height; y++) {
    for (let x = 0; x < image.width; x++) {
      const [r, g, b] = image.pixel(x, y);
      if (r > 200 && g < 80 && b > 200) magenta++;
      else {
        lo = Math.min(lo, g);
        hi = Math.max(hi, g);
      }
    }
  }
  const colors = { magenta, green_min: lo, green_max: hi };
  return { colors, ok: magenta === 0 && hi - lo > 40 };
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
    let shots = 0;
    const shot = async () => {
      const r = await send('Page.captureScreenshot', { format: 'png' });
      const png = Buffer.from(r.result.data, 'base64');
      if (opts.out) writeFileSync(join(opts.out, `terrain_${shots}.png`), png);
      shots++;
      return decodePng(png);
    };
    await send('Runtime.enable');
    await send('Log.enable');
    await send('Page.enable');
    // The viewport of the scene's 640x480 surface, so the canvas shows it unscaled; headless
    // Chrome leaves less than the window size to the page
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

    // The terrain shows once the graphs of its tiles are evaluated
    const deadline = Date.now() + TIMEOUT_MS;
    let last = null;
    while (!(last && last.ok) && Date.now() < deadline) {
      last = measure(await shot());
      if (!last.ok) await sleep(SHOT_MS);
    }
    const errors = log.filter((l) => /error/i.test(l));
    const invalid = log.filter((l) =>
      /Invalid (ShaderModule|RenderPipeline|CommandBuffer|Buffer|BindGroup|Texture)/.test(l));
    console.log(`screenshots: ${shots}, last: ${JSON.stringify(last && last.colors)}`);
    console.log(`terrain covers the image without cracks: ${!!(last && last.ok)}`);
    console.log(`errors: ${errors.length}, invalid WebGPU objects: ${invalid.length}`);
    status = last && last.ok && !errors.length && !invalid.length ? 0 : 1;
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
