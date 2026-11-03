#!/usr/bin/env node
import { spawn, spawnSync } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdtempSync, readFileSync, realpathSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { inflateSync } from 'node:zlib';

const USAGE = `Checks particle curves in the browser build in headless Chrome, where the curve
table of the particles is a storage buffer of the vertex stage like on native.

  node test/integration/web/particles.mjs [--chrome <path>] [--out <dir>] <bin dir>

<bin dir> holds boxrun.html, .js and .wasm, e.g.
.bake/local_env/<env>/wasm32-Emscripten/debug/bin, or under <output_dir>/local_env when
bake3's bake.json sets output_dir; "$(tools/integration.py local-env --env <env>)" prints the
local env folder either way. The page is served on a free localhost port and opened with
test/integration/web/particles.flecs, which the page preloads from the same server (?preload=),
at a fixed delta of 1/60 s. Three jets rise: on the left one that cools from white to red, in the
middle one without curves that stays white, on the right two that share their rows, from magenta
to cyan. At frame 180 the left jet changes to green and blue and the table grows. The check takes
screenshots until the left jet shows the new gradient and passes when one screenshot before
showed the warm gradient on the left, the last shows green and blue and no warm color on the
left, white in the middle and magenta and cyan on the right, and the log has no error and no
invalid WebGPU object. Exits 0 on a pass, 1 on a failure and 2 when Chrome or the page cannot
start.`;

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const SCENE = '/test/integration/web/particles.flecs';
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

// Pixels of each color class in the left, middle and right third of the image
function measure(image) {
  const thirds = [0, 1, 2].map(() => ({ warm: 0, green: 0, blue: 0, white: 0, magenta: 0,
    cyan: 0 }));
  for (let y = 0; y < image.height; y++) {
    for (let x = 0; x < image.width; x++) {
      const [r, g, b] = image.pixel(x, y);
      const c = thirds[Math.min(2, Math.floor((3 * x) / image.width))];
      if (Math.min(r, g, b) > 220) c.white++;
      else if (r > 150 && r > g + 40 && b < 90) c.warm++;
      else if (g > 150 && r < 140 && b < 150) c.green++;
      else if (b > 150 && r < 100 && g < 200) c.blue++;
      else if (r > 150 && b > 150 && g < 130) c.magenta++;
      else if (g > 150 && b > 150 && r < 120) c.cyan++;
    }
  }
  return { left: thirds[0], middle: thirds[1], right: thirds[2] };
}

// Pixels of a class that count as present
const SEEN = 40;

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
      if (opts.out) writeFileSync(join(opts.out, `particles_${shots}.png`), png);
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

    // The left jet changes at frame 180: warm before, green and blue after
    const deadline = Date.now() + TIMEOUT_MS;
    let warm = false, last = null, changed = false;
    while (!changed && Date.now() < deadline) {
      last = measure(await shot());
      if (!warm && last.left.warm > SEEN && last.left.green < SEEN) warm = true;
      changed = last.left.green > SEEN && last.left.blue > SEEN && last.left.warm < SEEN;
      await sleep(SHOT_MS);
    }
    // The browser compiles shaders after the frame that asks for them, so the frames before the
    // simulation shader is there skip the pools with this error, as in every particle scene
    const errors = log.filter((l) => /error/i.test(l) &&
      !/particle shader is not available/.test(l));
    const invalid = log.filter((l) =>
      /Invalid (ShaderModule|RenderPipeline|CommandBuffer|Buffer|BindGroup|Texture)/.test(l));
    const shared = last && last.right.magenta > SEEN && last.right.cyan > SEEN;
    const plain = last && last.middle.white > SEEN && last.middle.warm < SEEN &&
      last.middle.magenta < SEEN;
    console.log(`screenshots: ${shots}, last: ${JSON.stringify(last)}`);
    console.log(`left warm before the change: ${warm}, green and blue after: ${changed}`);
    console.log(`middle white: ${plain}, right magenta and cyan: ${shared}`);
    console.log(`errors: ${errors.length}, invalid WebGPU objects: ${invalid.length}`);
    status = warm && changed && plain && shared && !errors.length && !invalid.length ? 0 : 1;
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
