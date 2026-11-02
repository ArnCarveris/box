#!/usr/bin/env node
import { spawn, spawnSync } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdtempSync, readFileSync, realpathSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const USAGE = `Checks the box.audio module in headless Chrome with a scene that only uses the module
and script: test/integration/web/audio_module.flecs, a looping 440 Hz tone at a peak of 0.25 on
the bus music and a click that box.audio.play plays at load.

  node test/integration/web/audio_module.mjs [--chrome <path>] <bin dir>

<bin dir> holds boxrun.html, .js and .wasm (see audio_core.mjs). Without an autoplay flag the
check expects AudioStatus.state AudioWaitingForGesture before a gesture, then starts the audio
with a trusted click: the tone has to reach the worklet at 440 Hz and a peak of 0.25 with one
voice, the click dropped. A set of AudioBus.volume to 0.5 has to halve the level. A hidden page
has to fade out and suspend the context (AudioSuspended) and a visible one resume it. A capture
of one second (audio capture 1) has to land as a WAV in the file system of the page with the
tone, and a 500 ms busy loop on the main thread has to leave the loop playing. The commands run
through Module.cwrap('box_command_web_run'), the shell of the page. The log has no error and no
audio warning. Exits 0 on a pass, 1 on a failure and 2 when Chrome or the page cannot start.`;

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const SCENE = '/test/integration/web/audio_module.flecs';
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
const CHROME_DEFAULT = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const TONE = 440;
const LEVEL = 0.25;
const VOLUME = 0.5;
const STALL_MS = 500;
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

// Taps the worklet node with an AnalyserNode once, then measures the last 32768 frames: the
// frequency from the upward zero crossings, the peak, and the peak of the last 4096 frames
const MEASURE = `(async () => {
  const audio = Module.boxAudio;
  if (!window.boxTap) {
    window.boxTap = audio.ctx.createAnalyser();
    window.boxTap.fftSize = 32768;
    audio.node.connect(window.boxTap);
    await new Promise((r) => setTimeout(r, 1000));
  }
  const data = new Float32Array(window.boxTap.fftSize);
  window.boxTap.getFloatTimeDomainData(data);
  let peak = 0, first = -1, last = -1, count = 0, tail = 0, mean = 0;
  for (let i = 1; i < data.length; i++) {
    peak = Math.max(peak, Math.abs(data[i]));
    mean += data[i];
    if (i >= data.length - 4096) tail = Math.max(tail, Math.abs(data[i]));
    if (data[i - 1] < 0 && data[i] >= 0) {
      const at = i - 1 + data[i - 1] / (data[i - 1] - data[i]);
      if (first < 0) first = at; else count++;
      last = at;
    }
  }
  const rate = audio.ctx.sampleRate;
  return { frequency: count && last > first ? count * rate / (last - first) : 0, peak, tail,
    mean: mean / data.length, rate };
})()`;

// Hides or shows the page as a switch of tabs would, for the visibilitychange handler
const visibility = (state) => `(() => {
  Object.defineProperty(document, 'visibilityState', { value: '${state}',
    configurable: true });
  Object.defineProperty(document, 'hidden', { value: ${state === 'hidden'},
    configurable: true });
  document.dispatchEvent(new Event('visibilitychange'));
  return true;
})()`;

// Reads a WAV of the file system of the page: its format, frames and the peak and frequency
// of its left channel
const readWav = (path) => `(() => {
  if (!Module.FS.analyzePath(${JSON.stringify(path)}).exists) return null;
  const bytes = Module.FS.readFile(${JSON.stringify(path)});
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const tag = (at) => String.fromCharCode(...bytes.subarray(at, at + 4));
  if (bytes.length < 44) return { size: bytes.length };
  const rate = v.getUint32(24, true), channels = v.getUint16(22, true);
  const frames = v.getUint32(40, true) / 4;
  let peak = 0, first = -1, last = -1, count = 0, prev = 0;
  for (let i = 0; i < frames; i++) {
    const x = v.getInt16(44 + i * 4, true) / 32767;
    if (i > frames / 2) peak = Math.max(peak, Math.abs(x));
    if (i && prev < 0 && x >= 0) {
      const at = i - 1 + prev / (prev - x);
      if (first < 0) first = at; else count++;
      last = at;
    }
    prev = x;
  }
  return { riff: tag(0), wave: tag(8), rate, channels, bits: v.getUint16(34, true), frames,
    size: bytes.length, peak, frequency: count && last > first ? count * rate / (last - first) :
    0 };
})()`;

// Stalls the main thread for STALL_MS between two stats of the worklet
const STALL = `(async () => {
  const audio = Module.boxAudio;
  const before = await audio.stats();
  const start = performance.now();
  while (performance.now() - start < ${STALL_MS}) { }
  const after = await audio.stats();
  return { before, after, wall: (performance.now() - start) / 1000 };
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
    const status = async (member) => (await evaluate(shell(`get $ box.audio.AudioStatus.${member}`)));

    if (!(await waitFor('window.boxFrames >= 30', 20000))) {
      throw new Error('the scene did not start rendering');
    }

    // Before a gesture: AudioStatus waits for it, and the play of the click is dropped
    await sleep(500);
    const waiting = await status('state');
    console.log(`before the gesture: AudioStatus.state ${waiting}, context ` +
      `${await evaluate('Module.boxAudio.ctx.state')}`);
    expect(waiting === '"AudioWaitingForGesture"', 'AudioStatus does not wait for a gesture');

    for (const type of ['mousePressed', 'mouseReleased']) {
      await send('Input.dispatchMouseEvent',
        { type, x: 320, y: 240, button: 'left', buttons: type === 'mousePressed' ? 1 : 0,
          clickCount: 1 });
    }
    const running = await waitFor('Module.boxAudio.ctx.state === "running"', 5000);
    expect(running, 'the gesture did not start the audio');
    await waitFor(`${shell('get $ box.audio.AudioStatus.state')} === '"AudioRunning"'`, 3000);
    const state = await status('state');
    const stats = await evaluate('Module.boxAudio.stats()');
    console.log(`after the gesture: AudioStatus.state ${state}, worklet voices ${stats.voices}`);
    expect(state === '"AudioRunning"', 'AudioStatus is not AudioRunning after the gesture');
    expect(stats.voices === 1, 'the worklet does not play the tone alone, the click was not ' +
      'dropped or the tone does not play');

    // The tone of the scene plays at its level
    const tone = await evaluate(MEASURE);
    console.log(`tone: ${tone.frequency.toFixed(2)} Hz, peak ${tone.peak.toFixed(4)}, mean ` +
      `${tone.mean.toFixed(4)}, rate ${tone.rate}`);
    expect(Math.abs(tone.frequency - TONE) < 2, `the tone is not ${TONE} Hz`);
    expect(Math.abs(tone.peak - LEVEL) < 0.02, `the peak of the tone is not ${LEVEL}`);
    expect(Math.abs(tone.mean) < 0.02, 'the output has an offset, the click played');

    // A change of the bus halves the level
    const set = await evaluate(shell(`set music box.audio.AudioBus {volume: ${VOLUME}}`));
    await sleep(1000);
    const half = await evaluate(MEASURE);
    console.log(`after AudioBus.volume ${VOLUME} (${set}): peak ${half.tail.toFixed(4)}`);
    expect(Math.abs(half.tail - LEVEL * VOLUME) < 0.01, 'the volume of the bus did not apply');

    // A hidden page fades out and suspends, a visible one resumes
    await evaluate(visibility('hidden'));
    const suspended = await waitFor('Module.boxAudio.ctx.state === "suspended"', 3000);
    const hiddenState = await status('state');
    console.log(`hidden: context ${await evaluate('Module.boxAudio.ctx.state')}, ` +
      `AudioStatus.state ${hiddenState}, suspends ${await evaluate('Module.boxAudio.suspends')}`);
    expect(suspended, 'the hidden page did not suspend its audio');
    expect(hiddenState === '"AudioSuspended"', 'AudioStatus is not AudioSuspended when hidden');
    await evaluate(visibility('visible'));
    const resumed = await waitFor('Module.boxAudio.ctx.state === "running"', 3000);
    await sleep(500);
    const back = await evaluate(MEASURE);
    console.log(`visible: context ${await evaluate('Module.boxAudio.ctx.state')}, gain ` +
      `${await evaluate('Module.boxAudio.gain.gain.value')}, peak ${back.tail.toFixed(4)}`);
    expect(resumed, 'the visible page did not resume its audio');
    expect(Math.abs(back.tail - LEVEL * VOLUME) < 0.01, 'the tone did not come back');
    expect(await evaluate('Module.boxAudio.gain.gain.value') > 0.99, 'the gain did not ' +
      'fade back in');

    // A capture lands in the file system of the page
    const path = await evaluate(shell('audio capture 1'));
    console.log(`capture: ${path}`);
    expect(/^\/tmp\/audio_capture_\d+\.wav$/.test(path || ''), 'audio capture did not ' +
      'return a file');
    let wav = null;
    const deadline = Date.now() + 5000;
    while (Date.now() < deadline && !(wav && wav.frames)) {
      await sleep(200);
      wav = await evaluate(readWav(path));
    }
    console.log(`capture: ${JSON.stringify(wav)}`);
    expect(wav && wav.riff === 'RIFF' && wav.wave === 'WAVE' && wav.channels === 2 &&
      wav.bits === 16, 'the capture is not a 16 bit stereo WAV');
    expect(wav && wav.frames === wav.rate && wav.rate === tone.rate,
      'the capture is not one second at the rate of the context');
    expect(wav && Math.abs(wav.peak - LEVEL * VOLUME) < 0.01, 'the capture is not at the level');
    expect(wav && Math.abs(wav.frequency - TONE) < 2, 'the capture is not the tone');

    // A busy main thread leaves the loop playing
    const stall = await evaluate(STALL);
    console.log(`stall: wall ${stall.wall.toFixed(3)} s, quanta ` +
      `${stall.after.quanta - stall.before.quanta}, voices ${stall.before.voices} -> ` +
      `${stall.after.voices}`);
    expect(stall.after.quanta - stall.before.quanta >
      0.8 * stall.wall * stall.after.sampleRate / 128, 'the worklet stalled with the page');
    expect(stall.after.voices === 1, 'the loop stopped during the stall');
    const after = await evaluate(MEASURE);
    expect(Math.abs(after.tail - LEVEL * VOLUME) < 0.01, 'the loop changed after the stall');

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
