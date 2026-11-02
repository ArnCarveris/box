#!/usr/bin/env node
import { spawn, spawnSync } from 'node:child_process';
import { createServer } from 'node:net';
import { createServer as createHttpServer } from 'node:http';
import { existsSync, readFileSync } from 'node:fs';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const USAGE = `Checks the audio of the browser build in Safari on macOS, driven by safaridriver over
the W3C WebDriver HTTP API: a tone of the audio core, which runs from its own wasm in an
AudioWorklet, while a scene renders.

  node test/integration/web/audio_tone_safari.mjs [--safaridriver <path>] <bin dir>

<bin dir> holds boxrun.html, .js and .wasm, e.g.
.bake/local_env/<env>/wasm32-Emscripten/debug/bin, or under <output_dir>/local_env when
bake3's bake.json sets output_dir; "$(tools/integration.py local-env --env <env>)" prints the
local env folder either way. safaridriver comes from --safaridriver or $SAFARIDRIVER,
/usr/bin/safaridriver by default, and runs on a free localhost port. Safari needs remote
automation, enabled once with "sudo safaridriver --enable" and "Allow remote automation" in the
Developer section of Safari Settings, and a logged-in GUI session; the check stops with exit status 2 and says so
when either is missing. It prints the Safari and macOS versions and stops when Safari has no
WebGPU, which the page needs.

The page is served on a free localhost port without COOP/COEP headers and opened with
test/integration/web/audio_tone.flecs (?preload=) and ?tone=440. Safari WebDriver has no log
endpoint, so the server injects a script at the top of the <head> of boxrun.html, before the
page runs, which wraps console.log, info, warn and error, records uncaught errors and rejections,
and counts the frames the page draws; the check reads that log from the page. The check expects
the audio context suspended before a gesture while the scene renders, then clicks the canvas
with WebDriver Actions (pointer down and up). When Safari does not take the click as a gesture
(the context stays suspended without a failure), it tries a key press with Actions. It reports
how many gestures reached the page (Module.boxAudio.gestures) and how many called resume()
(Module.boxAudio.resumes), which tells a gesture that never arrives from a resume Safari
refuses. Once the context runs, an AnalyserNode on the worklet node has to measure 440 Hz at a
peak of 0.25, and a tone command sent from C (Module._box_audio_web_tone) has to change the
frequency to 660 Hz. The frames keep advancing and the log has no error and no box.audio
warning. Exits 0 on a pass, 1 on a failure and 2 when safaridriver, Safari or the page cannot
start.`;

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const SCENE = '/test/integration/web/audio_tone.flecs';
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
const SAFARIDRIVER_DEFAULT = '/usr/bin/safaridriver';
const ELEMENT = 'element-6066-11e4-a52e-4f735466cecf';
const TONE = 440;
const CHANGED = 660;
const LEVEL = 0.25;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Records the console and counts the frames the page draws, before the page runs
const INSTRUMENT = `(() => {
  window.boxFrames = 0;
  window.boxLog = [];
  const text = (args) => Array.prototype.map.call(args, (a) => {
    if (a instanceof Error) return a.stack || String(a);
    if (typeof a === 'object' && a !== null) { try { return JSON.stringify(a); } catch { } }
    return String(a);
  }).join(' ').replace(/\\x1b\\[[0-9;]*m/g, '');
  for (const level of ['log', 'info', 'warn', 'error']) {
    const original = console[level];
    console[level] = function () {
      window.boxLog.push((level === 'log' ? '' : '[' + level + '] ') + text(arguments));
      return original.apply(console, arguments);
    };
  }
  window.addEventListener('error', (e) => {
    window.boxLog.push('[exception] ' + (e.error && e.error.stack || e.message));
  });
  window.addEventListener('unhandledrejection', (e) => {
    window.boxLog.push('[exception] unhandled rejection: ' + (e.reason && e.reason.stack ||
      String(e.reason)));
  });
  if (!window.GPUCanvasContext) return;
  const getCurrentTexture = GPUCanvasContext.prototype.getCurrentTexture;
  GPUCanvasContext.prototype.getCurrentTexture = function () {
    window.boxFrames++;
    return getCurrentTexture.call(this);
  };
})();`;

// Taps the worklet node with an AnalyserNode once, then measures the frequency of the tone from
// its upward zero crossings and its peak, over the last 32768 frames
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
  let peak = 0, first = -1, last = -1, count = 0;
  for (let i = 1; i < data.length; i++) {
    peak = Math.max(peak, Math.abs(data[i]));
    if (data[i - 1] < 0 && data[i] >= 0) {
      const at = i - 1 + data[i - 1] / (data[i - 1] - data[i]);
      if (first < 0) first = at; else count++;
      last = at;
    }
  }
  const rate = audio.ctx.sampleRate;
  return { frequency: count && last > first ? count * rate / (last - first) : 0, peak, rate };
})()`;

const STATE = `({ state: Module.boxAudio.ctx.state, failed: Module.boxAudio.failed,
  gestures: Module.boxAudio.gestures, resumes: Module.boxAudio.resumes })`;

function parse(argv) {
  const opts = { safaridriver: process.env.SAFARIDRIVER || SAFARIDRIVER_DEFAULT, dir: null };
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--safaridriver') opts.safaridriver = argv[++i];
    else if (argv[i] === '-h' || argv[i] === '--help') { console.log(USAGE); process.exit(0); }
    else opts.dir = resolve(argv[i]);
  }
  if (!opts.dir || !existsSync(join(opts.dir, 'boxrun.html'))) {
    console.error(USAGE);
    process.exit(2);
  }
  return opts;
}

// Serves the page from the bin dir and the scene from the repository, without COOP/COEP
// headers, with the instrument script at the top of the <head> of the page
function serve(dir) {
  const server = createHttpServer((req, res) => {
    const path = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    const file = path === SCENE ? join(ROOT, path) : join(dir, path);
    if ((path !== SCENE && !file.startsWith(dir)) || !existsSync(file)) {
      res.writeHead(404); res.end(); return;
    }
    let body = readFileSync(file);
    if (path === '/boxrun.html') {
      const html = body.toString('utf8');
      const tag = `<script>${INSTRUMENT}</script>`;
      const head = html.search(/<head[^>]*>/i);
      body = head < 0 ? tag + html :
        html.replace(/<head[^>]*>/i, (m) => m + tag);
    }
    res.writeHead(200, { 'Content-Type': MIME[extname(file)] || 'application/octet-stream',
      'Cache-Control': 'no-store' });
    res.end(body);
  });
  return new Promise((r) => server.listen(0, '127.0.0.1', () => r(server)));
}

function freePort() {
  return new Promise((r) => {
    const s = createServer();
    s.listen(0, '127.0.0.1', () => { const port = s.address().port; s.close(() => r(port)); });
  });
}

// One WebDriver command; the value of the response, or throws with the WebDriver error
async function webdriver(base, method, path, body, ms = 60000) {
  const res = await fetch(base + path, {
    method,
    headers: body === undefined ? {} : { 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body),
    signal: AbortSignal.timeout(ms)
  });
  const json = await res.json().catch(() => ({}));
  const value = json.value;
  if (!res.ok || (value && value.error)) {
    const err = new Error(`${method} ${path}: ${value?.error ?? res.status}: ` +
      `${value?.message ?? ''}`);
    err.webdriver = value?.error;
    err.text = value?.message ?? '';
    throw err;
  }
  return value;
}

function macosVersion() {
  const sw = spawnSync('sw_vers', ['-productVersion'], { encoding: 'utf8' });
  const build = spawnSync('sw_vers', ['-buildVersion'], { encoding: 'utf8' });
  if (sw.error || sw.status !== 0) return 'unknown';
  return `${sw.stdout.trim()} (${build.stdout?.trim() ?? ''})`;
}

async function main() {
  const opts = parse(process.argv.slice(2));
  if (process.platform !== 'darwin') {
    console.error('safaridriver runs on macOS only');
    console.log('FAIL');
    process.exit(2);
  }
  const version = spawnSync(opts.safaridriver, ['--version'], { encoding: 'utf8' });
  if (version.error || version.status !== 0) {
    console.error(`${opts.safaridriver} --version failed: ` +
      `${version.error?.message ?? version.stderr.trim()}`);
    console.log('FAIL');
    process.exit(2);
  }
  console.log(`safaridriver: ${version.stdout.trim()}`);
  console.log(`macOS: ${macosVersion()}`);

  const server = await serve(opts.dir);
  const driverPort = await freePort();
  const driverLog = [];
  const driver = spawn(opts.safaridriver, ['-p', String(driverPort)],
    { stdio: ['ignore', 'pipe', 'pipe'] });
  driver.stdout.on('data', (d) => driverLog.push(String(d).trim()));
  driver.stderr.on('data', (d) => driverLog.push(String(d).trim()));
  const base = `http://127.0.0.1:${driverPort}`;
  let session = null;
  let status = 2;
  let log = [];
  const wd = (method, path, body, ms) =>
    webdriver(base, method, `/session/${session}${path}`, body, ms);
  // Runs an expression in the page and waits for its promise, if it is one
  const evaluate = async (expression) => {
    const value = await wd('POST', '/execute/async', {
      script: 'var done = arguments[arguments.length - 1];' +
        `Promise.resolve().then(function () { return (${expression}); }).then(done, ` +
        'function (e) { done({ boxError: String(e && e.stack || e) }); });',
      args: []
    });
    if (value && value.boxError) throw new Error(`the page threw: ${value.boxError}`);
    return value;
  };
  const waitFor = async (expression, ms) => {
    const deadline = Date.now() + ms;
    while (Date.now() < deadline) {
      try { if (await evaluate(expression)) return true; } catch { }
      await sleep(100);
    }
    return false;
  };
  const readLog = async () => {
    try { log = (await evaluate('window.boxLog || []')) ?? log; } catch { }
    return log;
  };

  try {
    let up = false;
    for (let i = 0; i < 100 && !up; i++) {
      try { up = !!(await webdriver(base, 'GET', '/status', undefined, 1000)); } catch { }
      if (!up) await sleep(100);
    }
    if (!up) throw new Error(`safaridriver did not start\n${driverLog.join('\n')}`);

    // Safari runs one automation session at a time and asks nothing of a logged-in user once
    // remote automation is on; without it the session is refused with a message that says so
    let created;
    try {
      created = await webdriver(base, 'POST', '/session', {
        capabilities: { alwaysMatch: { browserName: 'safari' } }
      }, 120000);
    } catch (err) {
      const text = `${err.message}\n${driverLog.join('\n')}`;
      if (/remote automation|--enable|authoriz/i.test(text)) {
        throw new Error(`Safari refused the automation session: ${err.message}\n` +
          'Remote automation is off. Run once on this host, as an admin:\n' +
          '  sudo safaridriver --enable\n' +
          'and turn on "Allow remote automation" in Safari Settings > Developer (Settings > ' +
          'Advanced > "Show features for web developers" shows that section).');
      }
      if (err.name === 'TimeoutError' || /timeout|abort/i.test(text)) {
        throw new Error('Safari did not open a session in time. Safari WebDriver needs a ' +
          `logged-in GUI session on this host.\n${text}`);
      }
      throw new Error(`Safari did not open a session: ${text}`);
    }
    session = created.sessionId;
    const caps = created.capabilities || {};
    console.log(`Safari: ${caps.browserVersion ?? 'unknown'} (${caps.browserName ?? 'safari'}, ` +
      `platform ${caps.platformName ?? 'unknown'})`);
    await wd('POST', '/timeouts', { script: 60000, pageLoad: 60000 });
    try { await wd('POST', '/window/rect', { width: 800, height: 640 }); } catch { }

    const port = server.address().port;
    await wd('POST', '/url', { url: `http://127.0.0.1:${port}/boxrun.html` +
      `?preload=${SCENE}&tone=${TONE}#${encodeURIComponent(SCENE)}` });

    const gpu = await evaluate('(async () => { if (!navigator.gpu) return "none"; ' +
      'const a = await navigator.gpu.requestAdapter(); return a ? "adapter" : "no adapter"; })()');
    console.log(`WebGPU: ${gpu}`);
    if (gpu !== 'adapter') {
      throw new Error(`this Safari has no WebGPU (${gpu}), the page cannot boot, so the audio ` +
        'cannot be checked');
    }
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
    expect(!isolation.isolated, 'the page is cross origin isolated');
    expect(!isolation.failed, `the audio failed (${isolation.failed})`);

    // Before a gesture: suspended, while the scene renders. The first frames wait for the
    // shaders to compile, so the window starts once the scene draws
    if (!(await waitFor('window.boxFrames >= 30', 20000))) {
      throw new Error('the scene did not start rendering');
    }
    const frames0 = await evaluate('window.boxFrames');
    await sleep(1000);
    const before = await evaluate(`Object.assign(${STATE}, { frames: window.boxFrames })`);
    console.log(`before the gesture: ${before.state}, failed ${before.failed}, ` +
      `frames ${frames0} -> ${before.frames}`);
    expect(before.state !== 'running', 'the audio runs before a gesture');
    expect(!before.failed, `the audio failed before the gesture (${before.failed})`);
    expect(before.frames - frames0 >= 10, 'the scene does not render before the gesture');
    const early = (await readLog()).filter((l) => /error/i.test(l));
    expect(!early.length, 'the log has an error before the gesture');

    // A click on the canvas through WebDriver Actions starts the context
    const canvas = (await wd('POST', '/element',
      { using: 'css selector', value: 'canvas' }))[ELEMENT];
    await wd('POST', '/actions', { actions: [{
      type: 'pointer', id: 'mouse', parameters: { pointerType: 'mouse' },
      actions: [
        { type: 'pointerMove', duration: 0, origin: { [ELEMENT]: canvas }, x: 0, y: 0 },
        { type: 'pointerDown', button: 0 },
        { type: 'pause', duration: 50 },
        { type: 'pointerUp', button: 0 }
      ]
    }] });
    await wd('DELETE', '/actions');
    let running = await waitFor('Module.boxAudio.ctx.state === "running"', 5000);
    let after = await evaluate(STATE);
    console.log(`after the click: ${after.state}, failed ${after.failed}, ` +
      `gestures ${after.gestures}, resumes ${after.resumes}`);
    let gesture = running ? 'click' : null;

    // Safari may not take a click of WebDriver as a gesture: try a key press on the canvas
    if (!running && !after.failed) {
      await evaluate('(document.getElementById("canvas").focus(), true)');
      await wd('POST', '/actions', { actions: [{
        type: 'key', id: 'keyboard',
        actions: [{ type: 'keyDown', value: 'a' }, { type: 'pause', duration: 50 },
          { type: 'keyUp', value: 'a' }]
      }] });
      await wd('DELETE', '/actions');
      running = await waitFor('Module.boxAudio.ctx.state === "running"', 5000);
      after = await evaluate(STATE);
      console.log(`after the key press: ${after.state}, failed ${after.failed}, ` +
        `gestures ${after.gestures}, resumes ${after.resumes}`);
      if (running) gesture = 'key press';
    }
    if (!running && !after.failed) {
      console.log(after.resumes ?
        'the gestures reached ctx.resume(), which Safari did not run: Safari does not take ' +
        'the input of WebDriver as a user gesture, a limit of the automation, not ' +
        'necessarily of the page' :
        after.gestures ?
          'the gestures reached the page but resume() was not called' :
          'no input of WebDriver reached the input handlers of the canvas');
    }
    expect(running, 'neither a click nor a key press of WebDriver started the audio');
    expect(!after.failed, `the audio failed after the gesture (${after.failed})`);
    if (gesture) console.log(`the audio started on a ${gesture}`);

    if (running) {
      const tone = await evaluate(MEASURE);
      console.log(`tone: ${tone.frequency.toFixed(2)} Hz, peak ${tone.peak.toFixed(4)}, ` +
        `sample rate ${tone.rate}`);
      expect(Math.abs(tone.frequency - TONE) < 2, `the tone is not ${TONE} Hz`);
      expect(Math.abs(tone.peak - LEVEL) < 0.02, `the peak of the tone is not ${LEVEL}`);

      // A command sent from C reaches the core
      const exported = await evaluate(`typeof Module._box_audio_web_tone === 'function'`);
      expect(exported, 'Module._box_audio_web_tone is not exported');
      if (exported) {
        await evaluate(`(Module._box_audio_web_tone(${CHANGED}, ${LEVEL}), true)`);
        await sleep(1000);
        const changed = await evaluate(MEASURE);
        console.log(`tone after the command: ${changed.frequency.toFixed(2)} Hz, peak ` +
          `${changed.peak.toFixed(4)}`);
        expect(Math.abs(changed.frequency - CHANGED) < 3, `the tone is not ${CHANGED} Hz`);
        expect(Math.abs(changed.peak - LEVEL) < 0.02, 'the level changed with the frequency');
      }
    }

    // The scene kept rendering
    const frames1 = await evaluate('window.boxFrames');
    await sleep(1000);
    const frames2 = await evaluate('window.boxFrames');
    console.log(`frames: ${before.frames} before the gesture, ${frames1} at the end, ` +
      `${frames2 - frames1} in the last second`);
    expect(frames2 - frames1 >= 10, 'the scene stopped rendering');

    await readLog();
    const errors = log.filter((l) => /error|\[exception\]/i.test(l));
    const warnings = log.filter((l) => /box\.audio/.test(l));
    console.log(`errors: ${errors.length}, audio warnings: ${warnings.length}`);
    expect(!errors.length, 'the log has an error');
    expect(!warnings.length, 'the log has an audio warning');
    status = failures.length ? 1 : 0;
    if (status) {
      console.log(failures.map((f) => `failed: ${f}`).join('\n'));
      console.log(log.join('\n'));
    }
  } catch (err) {
    console.error(String(err && err.message ? err.message : err));
    if (session) await readLog();
    console.log(log.join('\n'));
  } finally {
    if (session) {
      try { await webdriver(base, 'DELETE', `/session/${session}`, undefined, 10000); } catch { }
    }
    driver.kill('SIGTERM');
    server.close();
    await sleep(300);
  }
  console.log(status === 0 ? 'PASS' : 'FAIL');
  process.exit(status);
}

main();
