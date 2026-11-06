#!/usr/bin/env node
import { spawn, spawnSync } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdtempSync, readFileSync, realpathSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const USAGE = `Checks music streams of box.audio (AudioStream) in headless Chrome with
test/integration/web/audio_stream.flecs, whose stream plays test/assets/audio/tone.ogg, 480 Hz at a
peak of 0.5 in a loop of 0.25 s. The server serves the file by its path; the page does not
preload it, so the stream fetches it next to the page.

  node test/integration/web/audio_stream.mjs [--chrome <path>] <bin dir>

<bin dir> holds boxrun.html, .js and .wasm (see audio_core.mjs). The check expects the stream to
start at a trusted click, a capture of two seconds (audio.capture 2) with 480 Hz at 0.5 and no
step larger than the sine's over its loop crossings, at least 1.5 s decoded ahead in the worklet
and no underrun after a 500 ms stall of the main thread, the decode cost per frame
(AudioStreamStatus.decode_ms and the longest animation frame), the file fetched once and not in
the file system of the page, one error and a running page for a file served at no path, and
tone_960.ogg, which the page preloads with ?preload=, playing 960 Hz from the page's file system
without a fetch. Exits 0 on a pass, 1 on a failure and 2 when Chrome or the page cannot start.`;

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const SCENE = '/test/integration/web/audio_stream.flecs';
const FETCHED = '/test/assets/audio/tone.ogg';
const PRELOADED = '/test/assets/audio/tone_960.ogg';
const PRELOAD = [SCENE, PRELOADED];
const FILES = [SCENE, FETCHED, PRELOADED];
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
  '.ogg': 'audio/ogg' };
const CHROME_DEFAULT = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const TONE = 480;
const LEVEL = 0.5;
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

// The longest animation frame callback since a time, in ms, and the frames since
const longestFrame = (since) => `(() => {
  const t = window.boxFrameTimes.filter((x) => x[0] >= ${since});
  let work = 0;
  for (let i = 0; i < t.length; i++) work = Math.max(work, t[i][1]);
  return { work, frames: t.length };
})()`;

// Reads a capture of the file system of the page: its rate, the frequency and peak of the left
// channel after its first tenth of a second, and the largest step between two of its samples
const readCapture = (path) => `(() => {
  if (!Module.FS.analyzePath(${JSON.stringify(path)}).exists) return null;
  const bytes = Module.FS.readFile(${JSON.stringify(path)});
  if (bytes.length < 44) return { size: bytes.length };
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const rate = v.getUint32(24, true), frames = v.getUint32(40, true) / 4;
  const x = new Float32Array(frames);
  for (let i = 0; i < frames; i++) x[i] = v.getInt16(44 + i * 4, true) / 32767;
  let peak = 0, step = 0, first = -1, last = -1, count = 0, silent = 0;
  for (let i = Math.max(Math.floor(rate / 10), 1); i < frames; i++) {
    peak = Math.max(peak, Math.abs(x[i]));
    step = Math.max(step, Math.abs(x[i] - x[i - 1]));
    if (Math.abs(x[i]) < 0.001 && Math.abs(x[i - 1]) < 0.001) silent++;
    if (x[i - 1] < 0 && x[i] >= 0) {
      const at = i - 1 + x[i - 1] / (x[i - 1] - x[i]);
      if (first < 0) first = at; else count++;
      last = at;
    }
  }
  return { rate, frames, peak, step, silent,
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

// Serves the page from the bin dir and the scene and the Ogg files from the repository, without
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
    const preload = PRELOAD.map((f) => `preload=${encodeURIComponent(f)}`).join('&');
    const startedAt = Date.now();
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
    // A component of an entity as get prints it, parsed
    const component = async (entity, name) => {
      const out = await evaluate(shell(`get ${entity} box.audio.${name}`));
      try {
        return JSON.parse(out)[name];
      } catch {
        return { raw: out };
      }
    };
    const streamOf = (entity) => component(entity, 'AudioStreamStatus');
    const worklet = () => evaluate('Module.boxAudio.stats()');

    // Before the gesture the stream loops, so it starts and waits for the gesture
    const before = await streamOf('music');
    console.log(`before the gesture: ${JSON.stringify(before)}, page started ` +
      `${Date.now() - startedAt} ms ago`);
    expect(await evaluate('Module.boxAudio.ctx.state') !== 'running',
      'the audio ran before a gesture');
    expect(await evaluate(`!Module.FS.analyzePath(${JSON.stringify(FETCHED)}).exists`),
      'the fetched file is in the file system of the page');

    for (const type of ['mousePressed', 'mouseReleased']) {
      await send('Input.dispatchMouseEvent',
        { type, x: 320, y: 240, button: 'left', buttons: type === 'mousePressed' ? 1 : 0,
          clickCount: 1 });
    }
    expect(await waitFor('Module.boxAudio.ctx.state === "running"', 5000),
      'the gesture did not start the audio');
    expect(await waitFor(`${shell('get music box.audio.AudioStreamStatus')}` +
      `.includes('AudioStreamPlaying')`, 5000), 'the stream does not play');
    await sleep(1500);

    // The frequency and the level, and no step over the loop crossings, 8 of them in 2 s
    const rate = await evaluate('Module.boxAudio.ctx.sampleRate');
    const path = await evaluate(shell('audio.capture 2'));
    let capture = null;
    let deadline = Date.now() + 6000;
    while (Date.now() < deadline && !(capture && capture.frames)) {
      await sleep(200);
      capture = await evaluate(readCapture(path));
    }
    const sineStep = 2 * Math.PI * TONE / rate * LEVEL;
    console.log(`capture: ${JSON.stringify(capture)}, context rate ${rate}, largest step of ` +
      `the sine ${sineStep.toFixed(4)}`);
    expect(capture && capture.rate === rate, 'the capture is not at the rate of the context');
    expect(capture && Math.abs(capture.frequency - TONE) < 1, `the stream is not ${TONE} Hz`);
    expect(capture && Math.abs(capture.peak - LEVEL) < 0.02, `the peak is not ${LEVEL}`);
    expect(capture && capture.step < sineStep * 1.15 + 0.002,
      'a step of the capture is larger than the sine: a loop crossing is not seamless');
    expect(capture && capture.silent === 0, 'the capture has a gap');

    // The decode cost per frame, while the stream keeps its buffer full
    const since = await evaluate('performance.now()');
    await sleep(2000);
    const frames = await evaluate(longestFrame(since));
    const playing = await streamOf('music');
    console.log(`steady: ${JSON.stringify(playing)}, longest frame ${frames.work.toFixed(2)} ms ` +
      `over ${frames.frames} frames`);
    console.log(`decode per frame: last ${Number(playing.decode_ms).toFixed(3)} ms, largest ` +
      `${Number(playing.decode_max_ms).toFixed(3)} ms`);
    expect(playing.underruns === 0, 'the stream ran short before the stall');

    // A stall of 500 ms of the main thread: the worklet holds at least 1.5 s
    const ahead = await worklet();
    const stream = (ahead.streams || []).find((s) => s && s.open);
    console.log(`worklet before the stall: chunks ${ahead.chunks}, stream ` +
      `${JSON.stringify(stream)}, buffered ${playing.buffered} s`);
    expect(playing.buffered >= 1.5, 'less than 1.5 s is decoded ahead');
    expect(ahead.chunks * 4096 / playing.sample_rate >= 1.5,
      'the worklet holds less than 1.5 s');
    await evaluate('(() => { const t = performance.now(); while (performance.now() - t < 500) ' +
      '{ } })()');
    await sleep(1000);
    const stalled = await streamOf('music');
    const after = await worklet();
    const afterStream = (after.streams || []).find((s) => s && s.open);
    console.log(`after a 500 ms stall: underruns ${stalled.underruns}, worklet stream ` +
      `${JSON.stringify(afterStream)}`);
    expect(stalled.underruns === 0, 'the stall made the stream run short');
    expect(afterStream && afterStream.underruns === 0, 'the worklet counted an underrun');

    // The file was fetched once, by the stream, and is not part of the page
    console.log(`requests: ${JSON.stringify(requests)}`);
    expect(requests[FETCHED] === 1, 'the stream did not fetch its file exactly once');
    expect(await evaluate(`!Module.FS.analyzePath(${JSON.stringify(FETCHED)}).exists`),
      'the fetched file is in the file system of the page');

    // A file served at no path: one error, the page keeps running
    const errorsBefore = log.filter((l) => /error/i.test(l)).length;
    await evaluate(shell('rm music'));
    await sleep(300);
    await evaluate(shell('script_write missing_script ' + JSON.stringify(
      'using box.audio\nmissing {\n  AudioStream: {path: "test/assets/audio/nowhere.ogg"}\n}\n')));
    const failed = await waitFor(`${shell('get missing box.audio.AudioStreamStatus')}` +
      `.includes('AudioStreamFailed')`, 5000);
    const drawn = await evaluate('window.boxFrames');
    await sleep(1000);
    const drawnAfter = await evaluate('window.boxFrames');
    const missingErrors = log.filter((l) => /error/i.test(l)).slice(errorsBefore);
    console.log(`missing file: failed ${failed}, errors ${JSON.stringify(missingErrors)}, ` +
      `frames ${drawn} -> ${drawnAfter}`);
    expect(failed, 'the missing file did not fail');
    expect(missingErrors.filter((l) => /nowhere\.ogg/.test(l)).length === 1,
      'the missing file did not log exactly one error');
    expect(drawnAfter > drawn + 10, 'the page stopped after the missing file');

    // A file in the page's file system through ?preload= plays without a fetch
    await evaluate(shell('rm missing'));
    await sleep(300);
    const fetchesBefore = requests[PRELOADED] || 0;
    await evaluate(shell('script_write preloaded_script ' + JSON.stringify(
      'using box.audio\npreloaded {\n  AudioStream: {path: "test/assets/audio/tone_960.ogg"}\n}\n')));
    expect(await waitFor(`${shell('get preloaded box.audio.AudioStreamStatus')}` +
      `.includes('AudioStreamPlaying')`, 5000), 'the preloaded file does not play');
    await sleep(500);
    const path2 = await evaluate(shell('audio.capture 1'));
    let capture2 = null;
    deadline = Date.now() + 5000;
    while (Date.now() < deadline && !(capture2 && capture2.frames)) {
      await sleep(200);
      capture2 = await evaluate(readCapture(path2));
    }
    console.log(`preloaded: ${JSON.stringify(capture2)}, requests ${requests[PRELOADED]}`);
    expect(capture2 && Math.abs(capture2.frequency - 960) < 2, 'the preloaded file is not 960 Hz');
    expect(capture2 && Math.abs(capture2.peak - 0.25) < 0.02, 'the preloaded file is not at 0.25');
    expect((requests[PRELOADED] || 0) === fetchesBefore && fetchesBefore === 1,
      'the preloaded file was fetched again');

    const errors = log.filter((l) => /error/i.test(l) && !/nowhere\.ogg/.test(l));
    const warnings = log.filter((l) => /box\.audio/.test(l) && !/nowhere\.ogg/.test(l));
    console.log(`other errors: ${errors.length}, audio warnings: ${warnings.length}`);
    expect(!errors.length, 'the log has another error');
    expect(!warnings.length, 'the log has an audio warning');
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
