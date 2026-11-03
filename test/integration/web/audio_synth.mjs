#!/usr/bin/env node
import { spawn, spawnSync } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdtempSync, readFileSync, realpathSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const USAGE = `Checks the synth of box.audio in headless Chrome with
test/integration/web/audio_synth.flecs: Beep is a 660 Hz sine of 0.36 s, Hum three layers into a
filter envelope.

  node test/integration/web/audio_synth.mjs [--chrome <path>] <bin dir>

<bin dir> holds boxrun.html, .js and .wasm (see audio_core.mjs). After a trusted click starts the
audio, a capture of 2 seconds (audio capture 2) records Beep, which a script plays with
box.audio.play; its frequency, its level and its length are measured at the actual rate of the
context. Then 64 looping Hum voices play and the worklet renders 2000 quanta back to back
(Module.boxAudio.bench), whose load has to stay under real time. The log has no error and no
audio warning. Exits 0 on a pass, 1 on a failure and 2 when Chrome or the page cannot start.`;

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const SCENE = '/test/integration/web/audio_synth.flecs';
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
const CHROME_DEFAULT = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const FREQUENCY = 660;
const LENGTH = 0.36;
const LEVEL = 0.5;
const VOICES = 64;
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

// Reads the WAV of a capture: finds the beep, from the first to the last sample over 1 % of
// full scale, and measures its frequency from the upward zero crossings and its level in the
// middle of its hold, at the rate of the file
const readBeep = (path) => `(() => {
  if (!Module.FS.analyzePath(${JSON.stringify(path)}).exists) return null;
  const bytes = Module.FS.readFile(${JSON.stringify(path)});
  if (bytes.length < 44) return { size: bytes.length };
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const rate = v.getUint32(24, true), frames = v.getUint32(40, true) / 4;
  const l = new Float32Array(frames);
  let start = -1, end = -1;
  for (let i = 0; i < frames; i++) {
    l[i] = v.getInt16(44 + i * 4, true) / 32767;
    if (Math.abs(l[i]) > 0.01) {
      if (start < 0) start = i;
      end = i;
    }
  }
  if (start < 0) return { rate, frames, start };
  const from = start + Math.round(0.05 * rate), to = start + Math.round(0.3 * rate);
  let first = -1, last = -1, count = 0, peak = 0;
  for (let i = from + 1; i < to; i++) {
    peak = Math.max(peak, Math.abs(l[i]));
    if (l[i - 1] < 0 && l[i] >= 0) {
      const at = i - 1 + l[i - 1] / (l[i - 1] - l[i]);
      if (first < 0) first = at; else count++;
      last = at;
    }
  }
  return { rate, frames, start, end, length: (end - start + 1) / rate, peak,
    frequency: count && last > first ? count * rate / (last - first) : 0 };
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
    const rate = await evaluate('Module.boxAudio.ctx.sampleRate');
    console.log(`after the gesture: context ${await evaluate('Module.boxAudio.ctx.state')}, ` +
      `rate ${rate}`);

    // A beep from a script, recorded
    const path = await evaluate(shell('audio capture 2'));
    await sleep(300);
    const played = await evaluate(shell('script_write beep ' +
      JSON.stringify('using box.audio\nconst played: bool = box.audio.play(Beep, 1.0)\n')));
    console.log(`capture ${path}, beep: ${played}`);
    let beep = null;
    const deadline = Date.now() + 6000;
    while (Date.now() < deadline && !(beep && beep.frames)) {
      await sleep(250);
      beep = await evaluate(readBeep(path));
    }
    console.log(`beep: ${JSON.stringify(beep)}`);
    expect(beep && beep.start >= 0, 'the capture has no beep');
    expect(beep && beep.rate === rate, 'the capture is not at the rate of the context');
    expect(beep && Math.abs(beep.frequency - FREQUENCY) < 1,
      `the beep is not at ${FREQUENCY} Hz`);
    expect(beep && Math.abs(beep.peak - LEVEL) < 0.01, `the beep is not held at ${LEVEL}`);
    expect(beep && Math.abs(beep.length - LENGTH) < 0.01,
      `the envelope of the beep does not last ${LENGTH} s`);
    expect(beep && beep.end < beep.frames - 1, 'the beep does not end');

    // The load of 64 synth voices in the worklet
    let code = 'using box.audio\n';
    for (let i = 0; i < VOICES; i++) {
      code += `voice_${i} {\n  AudioSource: {sound: Hum, loop: true, volume: 0.02, ` +
        `pitch: ${(0.75 + 0.01 * i).toFixed(2)}}\n}\n`;
    }
    await evaluate(shell(`script_write voices ${JSON.stringify(code)}`));
    await sleep(1000);
    const bench = await evaluate(`Module.boxAudio.bench(${QUANTA})`);
    console.log(`load of ${bench.voices} synth voices in the worklet: ` +
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
