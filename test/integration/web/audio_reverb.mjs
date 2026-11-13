#!/usr/bin/env node
import { spawn, spawnSync } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdtempSync, readFileSync, realpathSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const USAGE = `Checks the reverb of box.audio in headless Chrome with
test/integration/web/audio_reverb.flecs: the bus world sends to the hall reverb (decay 2.4 s).

  node test/integration/web/audio_reverb.mjs [--chrome <path>] <bin dir>

<bin dir> holds boxrun.html, .js and .wasm (see audio_core.mjs). After a trusted click starts the
audio, a capture of 4 seconds (audio.capture 4) records a click that box.audio.play sends to the
reverb; the tail after the click has to ring with a decay near 2.4 s, measured at the actual
rate of the context. Then 32 looping voices play through the reverb and the worklet renders
2000 quanta back to back (Module.boxAudio.bench), whose load has to stay under real time. The
log has no error and no audio warning. Exits 0 on a pass, 1 on a failure and 2 when Chrome or
the page cannot start.`;

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const SCENE = '/test/integration/web/audio_reverb.flecs';
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
const CHROME_DEFAULT = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const DECAY = 2.4;
const VOICES = 32;
const QUANTA = 2000;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Counts the frames the page draws, before the page runs
const INSTRUMENT = `(() => {
  window.boxFrames = 0;
  if (!window.GPUCanvasContext) return;
  const getCurrentTexture = GPUCanvasContext.prototype.getCurrentTexture;
  GPUCanvasContext.prototype.getCurrentTexture = function () {
    window.boxFrames++;
    return getCurrentTexture.call(this);
  };
})();`;

// Runs a command line in the shell of the page and returns its output
const shell = (line) => `Module.cwrap('box_command_web_run', 'string', ['string'])(` +
  `${JSON.stringify(line)})`;

// Reads the WAV of a capture: finds the click, then measures the decay of the tail after it
// from the energy decay curve between -5 and -25 dB, at the rate of the file
const readTail = (path) => `(() => {
  if (!Module.FS.analyzePath(${JSON.stringify(path)}).exists) return null;
  const bytes = Module.FS.readFile(${JSON.stringify(path)});
  if (bytes.length < 44) return { size: bytes.length };
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const rate = v.getUint32(24, true), frames = v.getUint32(40, true) / 4;
  const l = new Float32Array(frames), r = new Float32Array(frames);
  let start = -1;
  for (let i = 0; i < frames; i++) {
    l[i] = v.getInt16(44 + i * 4, true) / 32767;
    r[i] = v.getInt16(46 + i * 4, true) / 32767;
    if (start < 0 && Math.abs(l[i]) > 0.1) start = i;
  }
  if (start < 0) return { rate, frames, start };
  const from = start + Math.round(0.012 * rate);
  const edc = new Float64Array(frames - from);
  let sum = 0;
  for (let i = frames - 1; i >= from; i--) {
    sum += l[i] * l[i] + r[i] * r[i];
    edc[i - from] = sum;
  }
  let at5 = -1, at25 = -1, peak = 0;
  for (let i = 0; i < edc.length && at25 < 0; i++) {
    const db = 10 * Math.log10(edc[i] / edc[0]);
    if (at5 < 0 && db <= -5) at5 = i;
    if (db <= -25) at25 = i;
  }
  for (let i = from; i < frames; i++) peak = Math.max(peak, Math.abs(l[i]), Math.abs(r[i]));
  return { rate, frames, start, tailPeak: peak, tailEnergy: edc[0],
    rt60: at5 >= 0 && at25 > at5 ? 3 * (at25 - at5) / rate : 0 };
})()`;

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

// Serves the page from the bin dir and the scene from the repository, without COOP/COEP headers
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

async function main() {
  const opts = parse(process.argv.slice(2));
  checkQuarantine(opts.chrome);
  const server = await serve(opts.dir);
  const profile = mkdtempSync(join(tmpdir(), 'box-web-'));
  const debugPort = 20000 + Math.floor(Math.random() * 20000);
  // No autoplay flag: Chrome keeps the context suspended until a gesture, which the check
  // asserts, so a gesture has to start the audio
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
      } else if (msg.method === 'Runtime.exceptionThrown') {
        log.push(`[exception] ${msg.params.exceptionDetails.exception?.description ??
          msg.params.exceptionDetails.text}`);
      }
    });
    const send = (method, params = {}) => new Promise((r) => {
      const n = ++id; pending.set(n, r); ws.send(JSON.stringify({ id: n, method, params }));
    });
    const evaluate = async (expression) => (await send('Runtime.evaluate',
      { expression, awaitPromise: true, returnByValue: true })).result?.result?.value;
    const waitFor = async (expression, ms) => {
      const deadline = Date.now() + ms;
      while (Date.now() < deadline) {
        if (await evaluate(expression)) return true;
        await sleep(100);
      }
      return false;
    };
    await send('Runtime.enable');
    await send('Log.enable');
    await send('Page.enable');
    await send('Page.addScriptToEvaluateOnNewDocument', { source: INSTRUMENT });
    await send('Emulation.setDeviceMetricsOverride',
      { width: 640, height: 480, deviceScaleFactor: 1, mobile: false });
    const port = server.address().port;
    await send('Page.navigate', { url: `http://127.0.0.1:${port}/boxrun.html` +
      `?preload=${SCENE}#${encodeURIComponent(SCENE)}` });
    if (!(await waitFor('!!(window.Module && Module.FS)', 20000))) {
      throw new Error('page did not start');
    }
    if (!(await waitFor('!!(Module.boxAudio && (Module.boxAudio.node || ' +
      'Module.boxAudio.failed))', 10000))) {
      throw new Error('the worklet of the audio core did not load');
    }

    const failures = [];
    const expect = (ok, what) => { if (!ok) failures.push(what); };
    if (!(await waitFor('window.boxFrames >= 30', 20000))) {
      throw new Error('the scene did not start rendering');
    }

    for (const type of ['mousePressed', 'mouseReleased']) {
      await send('Input.dispatchMouseEvent',
        { type, x: 320, y: 240, button: 'left', buttons: type === 'mousePressed' ? 1 : 0,
          clickCount: 1 });
    }
    const running = await waitFor('Module.boxAudio.ctx.state === "running"', 5000);
    expect(running, 'the gesture did not start the audio');
    await sleep(500);
    const reverbs = await evaluate('JSON.stringify(Module.boxAudio.effects || {})');
    console.log(`after the gesture: context ${await evaluate('Module.boxAudio.ctx.state')}, ` +
      `rate ${await evaluate('Module.boxAudio.ctx.sampleRate')}, reverb memory ${reverbs}`);
    expect(reverbs === '{"0":true}', 'the worklet did not take the memory of the reverb');

    // A click through the send, recorded with its tail
    const path = await evaluate(shell('audio.capture 4'));
    await sleep(300);
    const played = await evaluate(shell('script_write click ' +
      JSON.stringify('using box.audio\nconst played: bool = box.audio.play(click, 1.0)\n')));
    console.log(`capture ${path}, click: ${played}`);
    let tail = null;
    const deadline = Date.now() + 8000;
    while (Date.now() < deadline && !(tail && tail.frames)) {
      await sleep(250);
      tail = await evaluate(readTail(path));
    }
    console.log(`tail: ${JSON.stringify(tail)}`);
    expect(tail && tail.start >= 0, 'the capture has no click');
    expect(tail && tail.rate === await evaluate('Module.boxAudio.ctx.sampleRate'),
      'the capture is not at the rate of the context');
    expect(tail && tail.tailPeak > 0.001, 'no tail rings after the click');
    expect(tail && Math.abs(tail.rt60 - DECAY) < 0.25 * DECAY,
      `the tail does not decay in about ${DECAY} s`);

    // The load of the reverb and 32 voices in the worklet
    let code = 'using box.audio\n';
    for (let i = 0; i < VOICES; i++) {
      code += `voice_${i} {\n  Source: {sound: tone, loop: true, volume: 0.02, ` +
        `pitch: ${(0.75 + 0.01 * i).toFixed(2)}}\n}\n`;
    }
    await evaluate(shell(`script_write voices ${JSON.stringify(code)}`));
    await sleep(1000);
    const bench = await evaluate(`Module.boxAudio.bench(${QUANTA})`);
    console.log(`load of one reverb and ${bench.voices} voices in the worklet: ` +
      `${(bench.load * 100).toFixed(2)} % of real time (${QUANTA} quanta in ` +
      `${bench.seconds.toFixed(3)} s)`);
    expect(bench.voices === VOICES, `the worklet does not play ${VOICES} voices`);
    expect(bench.load > 0 && bench.load < 1, 'the worklet does not render in real time');

    const errors = log.filter((l) => /error/i.test(l));
    const warnings = log.filter((l) => /box\.audio/.test(l));
    const invalid = log.filter((l) =>
      /Invalid (ShaderModule|RenderPipeline|CommandBuffer|Buffer|BindGroup|Texture)/.test(l));
    console.log(`errors: ${errors.length}, audio warnings: ${warnings.length}, ` +
      `invalid WebGPU objects: ${invalid.length}`);
    expect(!errors.length, 'the log has an error');
    expect(!warnings.length, 'the log has an audio warning');
    expect(!invalid.length, 'the log has an invalid WebGPU object');
    status = failures.length ? 1 : 0;
    if (status) {
      console.log(failures.map((f) => `failed: ${f}`).join('\n'));
      console.log(log.join('\n'));
    }
    ws.close();
  } catch (err) {
    console.error(String(err && err.message ? err.message : err));
    console.log(log.join('\n'));
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
