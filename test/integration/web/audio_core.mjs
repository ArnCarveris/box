#!/usr/bin/env node
import { spawn, spawnSync } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdtempSync, readFileSync, realpathSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const USAGE = `Checks the audio of the browser build in headless Chrome: the mixer of the audio
core, which runs from its own wasm in an AudioWorklet, driven by command batches over the port.

  node test/integration/web/audio_core.mjs [--chrome <path>] <bin dir>

<bin dir> holds boxrun.html, .js and .wasm, e.g.
.bake/local_env/<env>/wasm32-Emscripten/debug/bin, or under <output_dir>/local_env when
bake3's bake.json sets output_dir; "$(tools/integration.py local-env --env <env>)" prints the
local env folder either way. The page is served on a free localhost port without COOP/COEP
headers and opened with test/integration/web/audio_core.flecs (?preload=), without an
autoplay flag. The check expects the audio context suspended before a gesture while the scene
renders and starts it with a trusted click of the DevTools protocol. It sends a sound of a
440 Hz sine at 0.25 and a command batch that plays it looped, taps the worklet with an
AnalyserNode and expects 440 Hz at a peak of 0.25. A volume command of 0.5 has to reach the
output as a ramp to a peak of 0.125 without a click. A 500 ms busy loop on the main thread has
to leave the audio clock and the quantum count advancing by its length and the voice playing.
A stop and a release have to come back as events, the worklet freeing the sound, and an order
with a cycle as a refusal. The frames keep advancing and the log has no error, no audio warning
and no invalid WebGPU object. Exits 0 on a pass, 1 on a failure and 2 when Chrome or the page
cannot start.`;

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const SCENE = '/test/integration/web/audio_core.flecs';
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

// Commands of src/audio/core/core.h, encoded as the core reads them: a header of a 16 bit
// type and a 16 bit size, then 32 bit fields, little endian
const ENCODE = `(() => {
  const header = (view, at, type, size) => { view.setUint16(at, type, true);
    view.setUint16(at + 2, size, true); };
  window.boxCommands = {
    play: (voice, sound, bus, volume, pan, pitch, flags) => {
      const v = new DataView(new ArrayBuffer(32));
      header(v, 0, 1, 32);
      v.setUint32(4, voice, true); v.setUint32(8, sound, true); v.setUint32(12, bus, true);
      v.setFloat32(16, volume, true); v.setFloat32(20, pan, true);
      v.setFloat32(24, pitch, true); v.setUint32(28, flags, true);
      return new Uint8Array(v.buffer);
    },
    stop: (voice) => {
      const v = new DataView(new ArrayBuffer(8));
      header(v, 0, 2, 8); v.setUint32(4, voice, true);
      return new Uint8Array(v.buffer);
    },
    voiceSet: (voice, param, value) => {
      const v = new DataView(new ArrayBuffer(16));
      header(v, 0, 3, 16); v.setUint32(4, voice, true); v.setUint32(8, param, true);
      v.setFloat32(12, value, true);
      return new Uint8Array(v.buffer);
    },
    order: (routes) => {
      const v = new DataView(new ArrayBuffer(8 + routes.length * 4));
      header(v, 0, 5, 8 + routes.length * 4); v.setUint32(4, routes.length, true);
      routes.forEach((r, i) => { v.setUint8(8 + i * 4, r[0]); v.setUint8(9 + i * 4, r[1]);
        v.setUint8(10 + i * 4, r[2]); });
      return new Uint8Array(v.buffer);
    },
    release: (sound) => {
      const v = new DataView(new ArrayBuffer(8));
      header(v, 0, 6, 8); v.setUint32(4, sound, true);
      return new Uint8Array(v.buffer);
    },
    batch: (...parts) => {
      const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
      let at = 0;
      for (const p of parts) { out.set(p, at); at += p.length; }
      return out.buffer;
    },
  };
})()`;

// Sends a sound of one second of a 440 Hz sine, a whole number of periods so the loop has no
// seam, and a batch that plays it looped on the master
const PLAY = `(async () => {
  const audio = Module.boxAudio;
  const rate = audio.ctx.sampleRate;
  const samples = new Float32Array(rate);
  for (let i = 0; i < rate; i++) samples[i] = ${LEVEL} * Math.sin(2 * Math.PI * ${TONE} * i / rate);
  const ok = await audio.sound(1, samples, 1, rate);
  audio.commands(boxCommands.batch(boxCommands.play(1, 1, 0, 1, 0, 1, 1)));
  return ok;
})()`;

// Taps the worklet node with an AnalyserNode once, then measures the last 32768 frames: the
// frequency from the upward zero crossings, the peak of the first and the last 4096 frames
// and the largest change between two frames
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
  let peak = 0, first = -1, last = -1, count = 0, step = 0, head = 0, tail = 0;
  for (let i = 1; i < data.length; i++) {
    peak = Math.max(peak, Math.abs(data[i]));
    step = Math.max(step, Math.abs(data[i] - data[i - 1]));
    if (i < 4096) head = Math.max(head, Math.abs(data[i]));
    if (i >= data.length - 4096) tail = Math.max(tail, Math.abs(data[i]));
    if (data[i - 1] < 0 && data[i] >= 0) {
      const at = i - 1 + data[i - 1] / (data[i - 1] - data[i]);
      if (first < 0) first = at; else count++;
      last = at;
    }
  }
  const rate = audio.ctx.sampleRate;
  return { frequency: count && last > first ? count * rate / (last - first) : 0, peak, head,
    tail, step, rate };
})()`;

// Stalls the main thread for STALL_MS between two stats of the worklet
const STALL = `(async () => {
  const audio = Module.boxAudio;
  const before = await audio.stats();
  const start = performance.now();
  const clock = audio.ctx.currentTime;
  while (performance.now() - start < ${STALL_MS}) { }
  const after = await audio.stats();
  return { before, after, wall: (performance.now() - start) / 1000,
    clock: audio.ctx.currentTime - clock };
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
    await send('Page.addScriptToEvaluateOnNewDocument', { source: ENCODE });
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
    const isolation = await evaluate('({ isolated: self.crossOriginIsolated, ' +
      'sab: typeof SharedArrayBuffer, failed: Module.boxAudio.failed, ' +
      'core: Module.boxAudio.coreSize })');
    console.log(`crossOriginIsolated: ${isolation.isolated}, SharedArrayBuffer: ` +
      `${isolation.sab}, core wasm: ${isolation.core} bytes`);
    expect(isolation.isolated === false, 'the page is cross origin isolated');
    expect(!isolation.failed, `the audio failed (${isolation.failed})`);

    // Before a gesture: suspended, while the scene renders. The first frames wait for the
    // shaders to compile, so the window starts once the scene draws
    if (!(await waitFor('window.boxFrames >= 30', 20000))) {
      throw new Error('the scene did not start rendering');
    }
    const frames0 = await evaluate('window.boxFrames');
    await sleep(1000);
    const before = await evaluate('({ state: Module.boxAudio.ctx.state, ' +
      'frames: window.boxFrames })');
    console.log(`before the gesture: ${before.state}, frames ${frames0} -> ${before.frames}`);
    expect(before.state !== 'running', 'the audio runs before a gesture');
    expect(before.frames - frames0 >= 10, 'the scene does not render before the gesture');
    const early = log.filter((l) => /error/i.test(l));
    expect(!early.length, 'the log has an error before the gesture');

    // A trusted click on the canvas starts the context
    for (const type of ['mousePressed', 'mouseReleased']) {
      await send('Input.dispatchMouseEvent',
        { type, x: 320, y: 240, button: 'left', buttons: type === 'mousePressed' ? 1 : 0,
          clickCount: 1 });
    }
    const running = await waitFor('Module.boxAudio.ctx.state === "running"', 5000);
    console.log(`after the gesture: ${await evaluate('Module.boxAudio.ctx.state')}`);
    expect(running, 'the gesture did not start the audio');

    // A sound and a command batch play through the worklet
    const took = await evaluate(PLAY);
    expect(took === true, 'the worklet did not take the sound');
    const tone = await evaluate(MEASURE);
    const sineStep = LEVEL * 2 * Math.sin(Math.PI * TONE / tone.rate);
    console.log(`voice: ${tone.frequency.toFixed(2)} Hz, peak ${tone.peak.toFixed(4)}, ` +
      `largest step ${tone.step.toFixed(5)} (sine ${sineStep.toFixed(5)}), sample rate ` +
      `${tone.rate}`);
    expect(Math.abs(tone.frequency - TONE) < 2, `the voice is not ${TONE} Hz`);
    expect(Math.abs(tone.peak - LEVEL) < 0.02, `the peak of the voice is not ${LEVEL}`);
    expect(tone.step < sineStep * 1.1, 'the voice has a discontinuity');

    // A volume command reaches the output as a ramp, within the window of the analyser
    await evaluate(`Module.boxAudio.commands(boxCommands.batch(` +
      `boxCommands.voiceSet(1, 0, ${VOLUME})))`);
    await sleep(300);
    const ramp = await evaluate(MEASURE);
    console.log(`volume ramp: peak ${ramp.head.toFixed(4)} -> ${ramp.tail.toFixed(4)}, ` +
      `largest step ${ramp.step.toFixed(5)}`);
    expect(Math.abs(ramp.head - LEVEL) < 0.02, 'the window does not start before the ramp');
    expect(Math.abs(ramp.tail - LEVEL * VOLUME) < 0.01, 'the volume did not reach the output');
    expect(ramp.step < sineStep * 1.1, 'the volume change clicks');

    // A busy main thread does not stop the worklet or its voices
    const stall = await evaluate(STALL);
    const quanta = stall.after.quanta - stall.before.quanta;
    const advance = stall.after.time - stall.before.time;
    const quantaTime = quanta * 128 / stall.after.sampleRate;
    console.log(`stall: wall ${stall.wall.toFixed(3)} s, audio clock ${advance.toFixed(3)} s, ` +
      `${quanta} quanta (${quantaTime.toFixed(3)} s), context clock ${stall.clock.toFixed(3)} s, ` +
      `voices ${stall.before.voices} -> ${stall.after.voices}`);
    expect(stall.wall >= STALL_MS / 1000, 'the busy loop was shorter than the stall');
    expect(Math.abs(advance - stall.wall) < 0.1, 'the audio clock did not follow the stall');
    expect(Math.abs(quantaTime - advance) < 0.02, 'the quanta did not follow the audio clock');
    expect(Math.abs(stall.clock - stall.wall) < 0.15, 'the context clock did not follow');
    expect(stall.after.voices === 1, 'the voice stopped during the stall');
    const afterStall = await evaluate(MEASURE);
    console.log(`voice after the stall: ${afterStall.frequency.toFixed(2)} Hz, peak ` +
      `${afterStall.peak.toFixed(4)}, largest step ${afterStall.step.toFixed(5)}`);
    expect(Math.abs(afterStall.frequency - TONE) < 2, 'the voice changed after the stall');
    expect(Math.abs(afterStall.peak - LEVEL * VOLUME) < 0.01,
      'the level changed after the stall');
    expect(afterStall.step < sineStep * 1.1, 'the voice has a discontinuity after the stall');

    // Events come back over the port: a refused order, the end of the voice and the free of
    // its sound, which the worklet frees in the heap of its core
    await evaluate(`(Module.boxAudio.events.length = 0, Module.boxAudio.commands(` +
      `boxCommands.batch(boxCommands.order([[1, 2, 255], [2, 1, 255], [0, 255, 255]]), ` +
      `boxCommands.stop(1), boxCommands.release(1))))`);
    await sleep(500);
    const events = await evaluate('Module.boxAudio.events');
    const stats = await evaluate('Module.boxAudio.stats()');
    const has = (type, id) => events.some((e) => e.type === type && e.id === id);
    console.log(`events: ${JSON.stringify(events)}, voices ${stats.voices}, sounds ` +
      `${stats.sounds}, batches ${stats.batches}, core memory ${stats.memory} bytes`);
    expect(events.some((e) => e.type === 4), 'the order with a cycle was not refused');
    expect(has(1, 1), 'the voice did not end');
    expect(has(3, 1), 'the sound was not freed');
    expect(stats.voices === 0 && stats.sounds === 0, 'the worklet still holds the voice');
    const silent = await evaluate(MEASURE);
    console.log(`after the stop: peak ${silent.tail.toFixed(5)}`);
    expect(silent.tail === 0, 'the output is not silent after the stop');
    const exported = await evaluate(`typeof Module._box_audio_web_apply === 'function' && ` +
      `typeof Module._box_audio_web_sound === 'function'`);
    expect(exported, 'Module._box_audio_web_apply or _box_audio_web_sound is not exported');

    // The scene kept rendering
    const frames1 = await evaluate('window.boxFrames');
    await sleep(1000);
    const frames2 = await evaluate('window.boxFrames');
    console.log(`frames: ${before.frames} before the gesture, ${frames1} at the end, ` +
      `${frames2 - frames1} in the last second`);
    expect(frames2 - frames1 >= 10, 'the scene stopped rendering');

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
