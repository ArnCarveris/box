#!/usr/bin/env node
import { spawn, spawnSync } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdtempSync, readFileSync, realpathSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const USAGE = `Checks every part of box.audio together in headless Chrome with
test/assets/audio/all_features.flecs: buses with a reverb send, synth clicks with a pitch
variance, a looping spatial synth drone on the left, music streamed from tone.ogg, sine_16.wav
played with play_at on the right and a capture at load. The page preloads the scene and both
files with ?preload=, so nothing is fetched while it plays.

  node test/integration/web/audio_all_features.mjs [--chrome <path>] <bin dir>

<bin dir> holds boxrun.html, .js and .wasm (see audio_core.mjs). The check expects the page to
wait for a gesture, then a trusted click to start the audio, both channels to carry a level, a
500 ms stall of the main thread with the worklet rendering on, no underrun of the stream and the
frame count advancing, the levels back after it, a hidden page to suspend and a visible one to
resume with its levels, and a log without an error, an audio warning or an invalid WebGPU
object. It prints the longest frame before and after the gesture. Exits 0 on a pass, 1 on a
failure and 2 when Chrome or the page cannot start.`;

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const SCENE = '/test/assets/audio/all_features.flecs';
const FILES = [SCENE, '/test/assets/audio/tone.ogg', '/test/assets/audio/sine_16.wav'];
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
  '.ogg': 'audio/ogg', '.wav': 'audio/wav' };
const CHROME_DEFAULT = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const STALL_MS = 500;
const LEVEL = 0.005;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Counts the frames the page draws and keeps the start and the length of each animation frame
// callback, the main loop, before the page runs
const INSTRUMENT = `(() => {
  window.boxFrames = 0;
  window.boxFrameTimes = [];
  const raf = window.requestAnimationFrame;
  window.requestAnimationFrame = function (cb) {
    return raf.call(window, (t) => {
      const start = performance.now();
      cb(t);
      window.boxFrameTimes.push([start, performance.now() - start]);
      if (window.boxFrameTimes.length > 4096) window.boxFrameTimes.shift();
    });
  };
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

// The longest animation frame callback since a time, in ms, the mean and the frames since
const frameTimes = (since) => `(() => {
  const t = window.boxFrameTimes.filter((x) => x[0] >= ${since});
  let work = 0, sum = 0;
  for (let i = 0; i < t.length; i++) { work = Math.max(work, t[i][1]); sum += t[i][1]; }
  return { work, mean: t.length ? sum / t.length : 0, frames: t.length };
})()`;

// Taps both channels of the worklet node with an AnalyserNode each, once, then measures the
// RMS and the peak of the last 16384 frames of each
const MEASURE = `(async () => {
  const audio = Module.boxAudio;
  if (!window.boxTaps) {
    const split = audio.ctx.createChannelSplitter(2);
    audio.node.connect(split);
    window.boxTaps = [0, 1].map((c) => {
      const a = audio.ctx.createAnalyser();
      a.fftSize = 16384;
      split.connect(a, c);
      return a;
    });
    await new Promise((r) => setTimeout(r, 600));
  }
  const data = new Float32Array(16384);
  const out = window.boxTaps.map((a) => {
    a.getFloatTimeDomainData(data);
    let sum = 0, peak = 0;
    for (let i = 0; i < data.length; i++) { sum += data[i] * data[i];
      peak = Math.max(peak, Math.abs(data[i])); }
    return { rms: Math.sqrt(sum / data.length), peak };
  });
  return { left: out[0], right: out[1], rate: audio.ctx.sampleRate };
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

// Stalls the main thread for STALL_MS between two stats of the worklet
const STALL = `(async () => {
  const audio = Module.boxAudio;
  const before = await audio.stats();
  const frames = window.boxFrames;
  const start = performance.now();
  while (performance.now() - start < ${STALL_MS}) { }
  const after = await audio.stats();
  return { before, after, frames, wall: (performance.now() - start) / 1000 };
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

// Serves the page from the bin dir and the scene and its files from the repository, without
// COOP/COEP headers, and counts the requests of each path
function serve(dir, requests) {
  const server = createServer((req, res) => {
    const path = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    requests[path] = (requests[path] || 0) + 1;
    const repo = FILES.includes(path);
    const file = repo ? join(ROOT, path) : join(dir, path);
    if ((!repo && !file.startsWith(dir)) || !existsSync(file)) {
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
  const requests = {};
  const server = await serve(opts.dir, requests);
  const profile = mkdtempSync(join(tmpdir(), 'box-web-'));
  const debugPort = 20000 + Math.floor(Math.random() * 20000);
  // No autoplay flag: Chrome keeps the context suspended until a gesture
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
    const preload = FILES.map((f) => `preload=${encodeURIComponent(f)}`).join('&');
    await send('Page.navigate', { url: `http://127.0.0.1:${port}/boxrun.html?${preload}` +
      `#${encodeURIComponent(SCENE)}` });
    if (!(await waitFor('!!(window.Module && Module.FS)', 20000))) {
      throw new Error('page did not start');
    }
    if (!(await waitFor('!!(Module.boxAudio && (Module.boxAudio.node || ' +
      'Module.boxAudio.failed))', 10000))) {
      throw new Error('the worklet of the audio core did not load');
    }
    if (!(await waitFor('window.boxFrames >= 30', 20000))) {
      throw new Error('the scene did not start rendering');
    }

    const failures = [];
    const expect = (ok, what) => { if (!ok) failures.push(what); };
    // A component as get prints it, parsed
    const component = async (entity, name) => {
      const out = await evaluate(shell(`get ${entity} box.audio.${name}`));
      try {
        return JSON.parse(out)[name];
      } catch {
        return { raw: out };
      }
    };
    const audioState = async () => (await component('$', 'AudioStatus')).state;
    const streamOf = () => component('music_track', 'AudioStreamStatus');
    const openStream = (stats) => (stats.streams || []).find((s) => s && s.open);

    // Before the gesture: the page waits, and the scene plays its timeline silently
    await sleep(1500);
    const quiet = await evaluate(frameTimes(0));
    const waiting = await audioState();
    console.log(`before the gesture: AudioStatus.state ${waiting}, frames ${quiet.frames}, ` +
      `longest ${quiet.work.toFixed(2)} ms, mean ${quiet.mean.toFixed(2)} ms`);
    expect(waiting === 'AudioWaitingForGesture', 'AudioStatus does not wait for a gesture');

    for (const type of ['mousePressed', 'mouseReleased']) {
      await send('Input.dispatchMouseEvent',
        { type, x: 320, y: 240, button: 'left', buttons: type === 'mousePressed' ? 1 : 0,
          clickCount: 1 });
    }
    expect(await waitFor('Module.boxAudio.ctx.state === "running"', 5000),
      'the gesture did not start the audio');
    const runningBy = Date.now() + 3000;
    while (Date.now() < runningBy && await audioState() !== 'AudioRunning') await sleep(100);
    expect(await audioState() === 'AudioRunning', 'AudioStatus is not AudioRunning');
    expect(await waitFor(`${shell('get music_track box.audio.AudioStreamStatus')}` +
      `.includes('AudioStreamPlaying')`, 5000), 'the music does not play');
    await sleep(1000);

    // Both channels carry a level: the engine on the left, the music, the clicks and the chime
    const since = await evaluate('performance.now()');
    const level = await evaluate(MEASURE);
    const stats = await evaluate('Module.boxAudio.stats()');
    console.log(`playing: left rms ${level.left.rms.toFixed(4)} peak ` +
      `${level.left.peak.toFixed(4)}, right rms ${level.right.rms.toFixed(4)} peak ` +
      `${level.right.peak.toFixed(4)}, rate ${level.rate}, worklet voices ${stats.voices}`);
    expect(level.left.rms > LEVEL, 'the left channel is silent');
    expect(level.right.rms > LEVEL, 'the right channel is silent');
    expect(level.left.peak < 0.96 && level.right.peak < 0.96, 'the output reaches the limiter');
    expect(stats.voices >= 2, 'the worklet does not play the engine and the music');
    await sleep(1000);
    const playing = await evaluate(frameTimes(since));
    console.log(`with audio: frames ${playing.frames}, longest ${playing.work.toFixed(2)} ms, ` +
      `mean ${playing.mean.toFixed(2)} ms`);

    // A stall of 500 ms of the main thread: the worklet renders on and the stream holds
    const stall = await evaluate(STALL);
    await sleep(1000);
    const framesAfter = await evaluate('window.boxFrames');
    const stream = await streamOf();
    const after = await evaluate('Module.boxAudio.stats()');
    const worklet = openStream(after);
    console.log(`stall: wall ${stall.wall.toFixed(3)} s, quanta ` +
      `${stall.after.quanta - stall.before.quanta}, voices ${stall.before.voices} -> ` +
      `${after.voices}, frames ${stall.frames} -> ${framesAfter}, stream underruns ` +
      `${stream.underruns}, worklet ${JSON.stringify(worklet)}`);
    expect(stall.after.quanta - stall.before.quanta >
      0.8 * stall.wall * stall.after.sampleRate / 128, 'the worklet stalled with the page');
    expect(stream.underruns === 0, 'the stall made the stream run short');
    expect(worklet && worklet.underruns === 0, 'the worklet counted an underrun');
    expect(framesAfter > stall.frames + 10, 'the frame count does not advance after the stall');
    const back = await evaluate(MEASURE);
    expect(back.left.rms > LEVEL && back.right.rms > LEVEL, 'the levels did not come back ' +
      'after the stall');

    // A hidden page fades out and suspends, a visible one resumes with its levels
    await evaluate(visibility('hidden'));
    const suspended = await waitFor('Module.boxAudio.ctx.state === "suspended"', 3000);
    const hiddenState = await audioState();
    console.log(`hidden: context ${await evaluate('Module.boxAudio.ctx.state')}, ` +
      `AudioStatus.state ${hiddenState}`);
    expect(suspended, 'the hidden page did not suspend its audio');
    expect(hiddenState === 'AudioSuspended', 'AudioStatus is not AudioSuspended when hidden');
    await evaluate(visibility('visible'));
    const resumed = await waitFor('Module.boxAudio.ctx.state === "running"', 3000);
    await sleep(800);
    const shown = await evaluate(MEASURE);
    const shownFrames = await evaluate('window.boxFrames');
    await sleep(500);
    const shownFramesAfter = await evaluate('window.boxFrames');
    console.log(`visible: context ${await evaluate('Module.boxAudio.ctx.state')}, left rms ` +
      `${shown.left.rms.toFixed(4)}, right rms ${shown.right.rms.toFixed(4)}, frames ` +
      `${shownFrames} -> ${shownFramesAfter}`);
    expect(resumed, 'the visible page did not resume its audio');
    expect(shown.left.rms > LEVEL && shown.right.rms > LEVEL, 'the levels did not come back');
    expect(shownFramesAfter > shownFrames, 'the frame count does not advance after a show');
    const last = openStream(await evaluate('Module.boxAudio.stats()'));
    expect(!last || last.underruns === 0, 'the stream ran short after the page was shown');

    // Nothing was fetched while it played: the scene and its files came with ?preload=
    console.log(`requests: ${JSON.stringify(requests)}`);
    expect(FILES.every((f) => requests[f] === 1), 'a file of the scene was fetched again');

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
