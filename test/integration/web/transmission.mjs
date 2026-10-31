#!/usr/bin/env node
import { spawn, spawnSync } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdtempSync, readFileSync, realpathSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { inflateSync } from 'node:zlib';

const USAGE = `Checks a rough transmissive shape in the browser build in headless Chrome, where
the render runs on the main thread and compile outcomes arrive after the frame.

  node test/integration/web/transmission.mjs [--chrome <path>] [--out <dir>] <bin dir>

<bin dir> holds boxrun.html, .js and .wasm, e.g.
.bake/local_env/<env>/wasm32-Emscripten/debug/bin, or under <output_dir>/local_env when
bake3's bake.json sets output_dir; "$(tools/integration.py local-env --env <env>)" prints the
local env folder either way. The page is served on a free localhost port and opened with
test/integration/web/transmission.flecs, which the page preloads from the same server
(?preload=), at a fixed delta of 1/60 s. At frame 30 the scene shows a rough, fully transmissive
glass in front of a bright flat wall, which requests etc/shaders/transmission.wgsl.

The check holds the animation frames of the page and runs them one at a time, with a screenshot
after each, and it records in the page in which frame the transmission shader was created, its
compile outcome arrived and its downsample pipeline was created. It passes when frames were
drawn with the glass before that pipeline existed, the center of the glass is never black and
never darker than its steady value (per channel, within a tolerance), the image becomes steady
and the log has no error and no invalid WebGPU object. Exits 0 on a pass, 1 on a failure and 2
when Chrome or the page cannot start.`;

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const SCENE = '/test/integration/web/transmission.flecs';
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
const CHROME_DEFAULT = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const DELTA_TIME = '0.016666667';
const TIMEOUT_MS = 90000;
const MAX_FRAMES = 400;
// Frames after the pipeline exists whose centers must agree for the image to be steady
const STEADY_FRAMES = 10;
const STEADY_TOLERANCE = 2;
// How much darker than its steady value a channel of the center may be
const TOLERANCE = 6;
// The largest channel of a center at or below this is black
const BLACK = 40;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Runs in the page before its scripts. It holds the callbacks of requestAnimationFrame, which
// drive the main loop of emscripten, until the check runs them with __box.advance(), so each
// screenshot shows one known frame. It counts the frames the canvas renders and records the
// frame (__box.frame, the advances so far) in which the transmission shader module was created,
// its compile outcome arrived and the first render pipeline with it was created.
const INSTRUMENT = `(() => {
  const raf = window.requestAnimationFrame.bind(window);
  const held = [];
  const box = window.__box = { frame: 0, canvas: 0, drawn: -1, module: -1, compiled: -1,
    pipeline: -1 };
  window.requestAnimationFrame = (cb) => { held.push(cb); return held.length; };
  // Runs the held callbacks in an animation frame of the browser and resolves two animation
  // frames later, once the canvas presented what they rendered
  box.advance = () => new Promise((resolve) => raf((t) => {
    box.frame++;
    for (const cb of held.splice(0)) {
      try { cb(t); } catch (err) { console.error('frame callback failed: ' + err); }
    }
    raf(() => raf(() => resolve({ frame: box.frame, drawn: box.drawn })));
  }));
  if (!window.GPUDevice) return;
  const modules = new WeakSet();
  const createShaderModule = GPUDevice.prototype.createShaderModule;
  GPUDevice.prototype.createShaderModule = function (desc) {
    const module = createShaderModule.call(this, desc);
    if (/transmission\\.wgsl/.test((desc && desc.label) || '')) {
      modules.add(module);
      if (box.module < 0) box.module = box.frame;
      module.getCompilationInfo().then(() => {
        if (box.compiled < 0) box.compiled = box.frame;
      });
    }
    return module;
  };
  const createRenderPipeline = GPUDevice.prototype.createRenderPipeline;
  GPUDevice.prototype.createRenderPipeline = function (desc) {
    if (box.pipeline < 0 && desc && desc.fragment && modules.has(desc.fragment.module)) {
      box.pipeline = box.frame;
    }
    return createRenderPipeline.call(this, desc);
  };
  const getCurrentTexture = GPUCanvasContext.prototype.getCurrentTexture;
  GPUCanvasContext.prototype.getCurrentTexture = function () {
    box.canvas++;
    box.drawn = box.frame;
    return getCurrentTexture.call(this);
  };
})();`;

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

// The color at the center of the image, where the glass is centered: the mean of its 3x3 pixels
function center(image) {
  const cx = Math.floor(image.width / 2), cy = Math.floor(image.height / 2);
  const sum = [0, 0, 0];
  for (let y = cy - 1; y <= cy + 1; y++) {
    for (let x = cx - 1; x <= cx + 1; x++) {
      image.pixel(x, y).forEach((v, c) => { sum[c] += v; });
    }
  }
  return sum.map((v) => v / 9);
}

const rgb = (c) => (c ? c.map((v) => v.toFixed(0)).join(',') : 'none');

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
    const shot = async (frame) => {
      const r = await send('Page.captureScreenshot', { format: 'png' });
      const png = Buffer.from(r.result.data, 'base64');
      if (opts.out) writeFileSync(join(opts.out, `transmission_${frame}.png`), png);
      return decodePng(png);
    };
    await send('Runtime.enable');
    await send('Log.enable');
    await send('Page.enable');
    await send('Page.addScriptToEvaluateOnNewDocument', { source: INSTRUMENT });
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
    if (!(await evaluate('!!(window.__box && window.GPUDevice)'))) {
      throw new Error('the page was not instrumented');
    }

    // One screenshot per frame, each labeled with the frame the canvas rendered last
    const shots = [];
    const deadline = Date.now() + TIMEOUT_MS;
    let box = null, steady = false;
    while (!steady && Date.now() < deadline) {
      const step = await evaluate('window.__box.advance()');
      if (!step) throw new Error('the page stopped running frames');
      const image = await shot(step.frame);
      shots.push({ frame: step.frame, drawn: step.drawn, color: center(image) });
      box = await evaluate('({...window.__box})');
      const after = box.pipeline >= 0 ? shots.filter((s) => s.drawn >= box.pipeline) : [];
      if (after.length >= STEADY_FRAMES) {
        const last = after.slice(-STEADY_FRAMES);
        steady = [0, 1, 2].every((c) => {
          const values = last.map((s) => s.color[c]);
          return Math.max(...values) - Math.min(...values) <= STEADY_TOLERANCE;
        });
      }
      if (step.frame >= MAX_FRAMES) break;
    }
    if (!box) throw new Error('no frame ran');

    // The glass is drawn from the frame that created the transmission shader; the frames drawn
    // before the one that created the downsample pipeline sampled the snapshot without it
    const shape = box.module >= 0 ? shots.filter((s) => s.drawn >= box.module) : [];
    const before = shape.filter((s) => box.pipeline < 0 || s.drawn < box.pipeline);
    const background = shots.filter((s) => s.drawn >= 0 && s.drawn < box.module).pop();
    const target = steady ? shots[shots.length - 1].color : null;
    const darker = target ? shape.filter((s) => s.color.some((v, c) => v < target[c] - TOLERANCE))
      : [];
    const black = shape.filter((s) => Math.max(...s.color) <= BLACK);
    // The glass lowers the green of the wall: a frame before the pipeline that does not show it
    // proves nothing
    const unseen = background ? before.filter((s) => s.color[1] > background.color[1] - 20) : [];
    const errors = log.filter((l) => /error/i.test(l));
    const invalid = log.filter((l) =>
      /Invalid (ShaderModule|RenderPipeline|CommandBuffer|Buffer|BindGroup|Texture)/.test(l));
    console.log(`frames: ${box.frame}, canvas frames: ${box.canvas}, screenshots: ${shots.length}`);
    console.log(`transmission.wgsl created in frame ${box.module}, compiled after frame ` +
      `${box.compiled}, downsample pipeline created in frame ${box.pipeline}`);
    console.log(`frames with the glass before the pipeline: ${before.length}` +
      (before.length ? ` (${before.map((s) => s.drawn).join(', ')})` : '') +
      `, not showing the glass: ${unseen.length}`);
    console.log(`center: wall ${rgb(background && background.color)}, first frame ` +
      `${rgb(shape.length ? shape[0].color : null)}, steady ${rgb(target)}`);
    console.log(`darker than steady: ${darker.length}, black: ${black.length}, steady: ${steady}`);
    console.log(`errors: ${errors.length}, invalid WebGPU objects: ${invalid.length}`);
    status = box.module >= 0 && box.pipeline > box.module && before.length && background &&
      !unseen.length && steady && !darker.length && !black.length && !errors.length &&
      !invalid.length ? 0 : 1;
    if (status) {
      console.log(shots.map((s) => `frame ${s.frame} drawn ${s.drawn} center ${rgb(s.color)}`)
        .join('\n'));
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
