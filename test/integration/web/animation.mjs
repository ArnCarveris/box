#!/usr/bin/env node
import { spawn, spawnSync } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdtempSync, readFileSync, realpathSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { inflateSync } from 'node:zlib';

const USAGE = `Checks keyframe animation in the browser build in headless Chrome, where
AnimatorPlay and the render run on the main thread.

  node test/integration/web/animation.mjs [--chrome <path>] [--out <dir>] <bin dir>

<bin dir> holds boxrun.html, .js and .wasm, e.g.
.bake/local_env/<env>/wasm32-Emscripten/debug/bin, or under <output_dir>/local_env when
bake3's bake.json sets output_dir; "$(tools/integration.py local-env --env <env>)" prints the
local env folder either way. The page is served on a free localhost port and opened with
test/integration/web/animation.flecs, which the page preloads from the same server
(?preload=), at a fixed delta of 1/60 s. A white square moves between a grey marker and a green
one in ping pong over 2 s and a red square fades to blue and back. The check takes screenshots
for 6 s and passes when the square was seen near both markers and in between, the color both
mostly red and mostly blue, and the log has no error and no invalid WebGPU object. Exits 0 on a
pass, 1 on a failure and 2 when Chrome or the page cannot start.`;

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const SCENE = '/test/integration/web/animation.flecs';
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
const CHROME_DEFAULT = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const DELTA_TIME = '0.016666667';
const TIMEOUT_MS = 90000;
const WATCH_MS = 6000;
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
    const file = path === SCENE ? join(ROOT, path) : join(dir, path);
    if ((path !== SCENE && !file.startsWith(dir)) || !existsSync(file)) {
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

// The place and colors of the shapes the check measures, null for a shape it cannot find. They
// lie within 120 pixels of the center vertically
function measure(image) {
  const sums = { white: [0, 0], grey: [0, 0], green: [0, 0], shifter: [0, 0, 0, 0] };
  const cy = Math.floor(image.height / 2);
  for (let y = Math.max(0, cy - 120); y < Math.min(image.height, cy + 120); y++) {
    for (let x = 0; x < image.width; x++) {
      const [r, g, b] = image.pixel(x, y);
      if (Math.min(r, g, b) > 200) { sums.white[0] += x; sums.white[1]++; }
      else if (r > 90 && r < 170 && Math.abs(r - g) < 12 && Math.abs(g - b) < 12) {
        sums.grey[0] += x; sums.grey[1]++;
      } else if (g > 120 && r < 80 && b < 80) { sums.green[0] += x; sums.green[1]++; }
      else if (g < 60 && r + b > 150) {
        sums.shifter[0] += r; sums.shifter[1] += g; sums.shifter[2] += b; sums.shifter[3]++;
      }
    }
  }
  const mean = (s) => (s[1] ? s[0] / s[1] : null);
  const n = sums.shifter[3];
  return {
    square: mean(sums.white), start: mean(sums.grey), target: mean(sums.green),
    color: n ? [sums.shifter[0] / n, sums.shifter[1] / n, sums.shifter[2] / n] : null,
  };
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
      if (opts.out) writeFileSync(join(opts.out, `animation_${shots}.png`), png);
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
    await send('Page.navigate',
      { url: `http://127.0.0.1:${port}/boxrun.html?preload=${SCENE}#${hash}` });
    for (let i = 0; i < 100 && !(await evaluate('!!(window.Module && Module.FS)')); i++) {
      await sleep(200);
    }
    if (!(await evaluate('!!(window.Module && Module.FS)'))) throw new Error('page did not start');

    // Progress of the square from the start to the target marker and the color of the other
    // one, for WATCH_MS once both shapes are found or until TIMEOUT_MS
    const samples = [];
    const deadline = Date.now() + TIMEOUT_MS;
    let first = 0;
    while (Date.now() < deadline && (!first || Date.now() - first < WATCH_MS)) {
      const m = measure(await shot());
      if (m.square !== null && m.start !== null && m.target !== null && m.color) {
        first = first || Date.now();
        samples.push({ p: (m.square - m.start) / (m.target - m.start), color: m.color });
      }
      await sleep(SHOT_MS);
    }
    const near_start = samples.filter((s) => s.p < 0.2).length;
    const near_target = samples.filter((s) => s.p > 0.8).length;
    const between = samples.filter((s) => s.p > 0.3 && s.p < 0.7).length;
    const red = samples.filter((s) => s.color[0] > 2 * s.color[2]).length;
    const blue = samples.filter((s) => s.color[2] > 2 * s.color[0]).length;
    const errors = log.filter((l) => /error/i.test(l));
    const invalid = log.filter((l) =>
      /Invalid (ShaderModule|RenderPipeline|CommandBuffer|Buffer|BindGroup|Texture)/.test(l));
    console.log(`screenshots: ${shots}, measured: ${samples.length}`);
    console.log(`square near start: ${near_start}, between: ${between}, near target: ` +
      `${near_target}`);
    console.log(`color mostly red: ${red}, mostly blue: ${blue}`);
    console.log(`errors: ${errors.length}, invalid WebGPU objects: ${invalid.length}`);
    status = near_start && near_target && between && red && blue && !errors.length &&
      !invalid.length ? 0 : 1;
    if (status) {
      console.log(samples.map((s) => `p ${s.p.toFixed(3)} rgb ${s.color.map((c) =>
        c.toFixed(0)).join(',')}`).join('\n'));
      console.log(log.join('\n'));
    }
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
