#!/usr/bin/env node
import { spawn, spawnSync } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdtempSync, readFileSync, realpathSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const USAGE = `Checks sound files of box.audio in headless Chrome with
test/integration/web/audio_sample.flecs, whose SoundFile decodes one second of 440 Hz at a peak
of 0.25 and 44100 Hz (test/assets/audio/tone_44100.wav). The page preloads the scene and the
sounds with ?preload=.

  node test/integration/web/audio_sample.mjs [--chrome <path>] <bin dir>

<bin dir> holds boxrun.html, .js and .wasm (see audio_core.mjs). After a trusted click the check
records two seconds (audio.capture 2) while a script plays the tone once: the capture has to
hold 440 Hz for one second at the actual rate of the context. It then sets the SoundFile of 2
seconds of stereo WAV and FLAC (long_stereo.wav, .flac) and reports the longest animation frame
callback while each decodes, the stall of the inline job, against the longest one before, and
the longest gap between two frames. Last it deletes a sound whose looping source plays: the worklet has to drop the voice
and free the sound. The log has no error and no audio warning. Exits 0 on a pass, 1 on a
failure and 2 when Chrome or the page cannot start.`;

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const SCENE = '/test/integration/web/audio_sample.flecs';
const SOUNDS = ['tone_44100.wav', 'long_stereo.wav', 'long_stereo.flac', 'loop.wav']
  .map((f) => `/test/assets/audio/${f}`);
const FILES = [SCENE, ...SOUNDS];
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
const CHROME_DEFAULT = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const TONE = 440;
const LEVEL = 0.25;
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

// The longest animation frame callback and the longest gap between two of them since a time,
// in ms
const longestFrame = (since) => `(() => {
  const t = window.boxFrameTimes.filter((x) => x[0] >= ${since});
  let gap = 0, work = 0;
  for (let i = 0; i < t.length; i++) {
    work = Math.max(work, t[i][1]);
    if (i) gap = Math.max(gap, t[i][0] - t[i - 1][0]);
  }
  return { gap, work, frames: t.length };
})()`;

// Reads a capture of the file system of the page: its rate, the first and last frame of the
// left channel above 0.001, the duration between them and the frequency and peak of its middle
const readCapture = (path) => `(() => {
  if (!Module.FS.analyzePath(${JSON.stringify(path)}).exists) return null;
  const bytes = Module.FS.readFile(${JSON.stringify(path)});
  if (bytes.length < 44) return { size: bytes.length };
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const rate = v.getUint32(24, true), frames = v.getUint32(40, true) / 4;
  const x = new Float32Array(frames);
  for (let i = 0; i < frames; i++) x[i] = v.getInt16(44 + i * 4, true) / 32767;
  let on = -1, off = -1;
  for (let i = 0; i < frames; i++) if (Math.abs(x[i]) > 0.001) { if (on < 0) on = i; off = i; }
  let peak = 0, first = -1, last = -1, count = 0;
  const from = on + Math.floor(rate / 10), to = off - Math.floor(rate / 10);
  for (let i = Math.max(from, 1); i < to; i++) {
    peak = Math.max(peak, Math.abs(x[i]));
    if (x[i - 1] < 0 && x[i] >= 0) {
      const at = i - 1 + x[i - 1] / (x[i - 1] - x[i]);
      if (first < 0) first = at; else count++;
      last = at;
    }
  }
  return { rate, frames, on, off, duration: on >= 0 ? (off - on) / rate : 0, peak,
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

// Serves the page from the bin dir and the scene and its sounds from the repository, without
// COOP/COEP headers
function serve(dir) {
  const server = createServer((req, res) => {
    const path = decodeURIComponent(new URL(req.url, 'http://x').pathname);
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
    // The member of a component of an entity, a JSON string as get prints it
    const member = async (entity, component, name) => {
      const out = await evaluate(shell(`get ${entity} box.audio.${component}`));
      try {
        return JSON.stringify(JSON.parse(out)[component][name]);
      } catch {
        return out;
      }
    };
    const soundState = (entity) => member(entity, 'SoundStatus', 'state');

    const toneState = await soundState('tone');
    console.log(`tone: SoundStatus.state ${toneState}, ` +
      `rate ${await member('tone', 'SoundStatus', 'sample_rate')}`);
    expect(toneState === '"SoundReady"', 'the preloaded WAV did not decode');

    for (const type of ['mousePressed', 'mouseReleased']) {
      await send('Input.dispatchMouseEvent',
        { type, x: 320, y: 240, button: 'left', buttons: type === 'mousePressed' ? 1 : 0,
          clickCount: 1 });
    }
    expect(await waitFor('Module.boxAudio.ctx.state === "running"', 5000),
      'the gesture did not start the audio');
    await waitFor(`${shell('get $ box.audio.AudioStatus')}.includes('AudioRunning')`, 3000);

    // One play of the tone during a capture of two seconds: one second of 440 Hz at the rate of
    // the context, whatever it is
    const path = await evaluate(shell('audio.capture 2'));
    // The capture starts with the next frames, so the play has to come after them
    await sleep(250);
    await evaluate(shell('script_write player_script ' +
      JSON.stringify('using box.audio\nplayer {\n  AudioSource: {sound: tone}\n}\n')));
    let capture = null;
    const deadline = Date.now() + 6000;
    while (Date.now() < deadline && !(capture && capture.frames)) {
      await sleep(200);
      capture = await evaluate(readCapture(path));
    }
    const rate = await evaluate('Module.boxAudio.ctx.sampleRate');
    console.log(`play: ${JSON.stringify(capture)}, context rate ${rate}`);
    expect(capture && capture.rate === rate, 'the capture is not at the rate of the context');
    expect(capture && Math.abs(capture.frequency - TONE) < 2, `the sound is not ${TONE} Hz`);
    expect(capture && Math.abs(capture.duration - 1) < 0.02, 'the sound is not one second long');
    expect(capture && Math.abs(capture.peak - LEVEL) < 0.02, `the peak is not ${LEVEL}`);

    // The stall of the decode of two seconds of stereo, which runs inline on the main thread
    for (const [name, file] of [['wav', 'long_stereo.wav'], ['flac', 'long_stereo.flac']]) {
      await sleep(500);
      const since = await evaluate('performance.now()');
      await sleep(1000);
      const before = await evaluate(longestFrame(since));
      const start = await evaluate('performance.now()');
      await evaluate(shell(`script_write long_${name}_script ` + JSON.stringify(
        `using box.audio\nlong_${name} {\n  SoundFile: {path: "test/assets/audio/${file}"}\n}\n`)));
      const ready = await waitFor(`${shell(`get long_${name} box.audio.SoundStatus`)}` +
        `.includes('SoundReady')`, 5000);
      await sleep(500);
      const during = await evaluate(longestFrame(start));
      console.log(`decode of 2 s stereo ${name.toUpperCase()}: longest frame ` +
        `${during.work.toFixed(1)} ms (${before.work.toFixed(1)} ms before), longest gap ` +
        `${during.gap.toFixed(1)} ms (${before.gap.toFixed(1)} ms before), ready ${ready}`);
      expect(ready, `the ${name} of two seconds did not decode`);
    }

    // Deleting a sound while its looping source plays fades the voice and frees the sound
    await evaluate(shell('script_write loop_script ' + JSON.stringify(
      'using box.audio\nloop_sound {\n  SoundFile: {path: "test/assets/audio/loop.wav"}\n}\n')));
    await evaluate(shell('script_write looper_script ' + JSON.stringify(
      'using box.audio\nlooper {\n  AudioSource: {sound: loop_sound, loop: true}\n}\n')));
    await sleep(1000);
    const playing = await evaluate('Module.boxAudio.stats()');
    await evaluate(shell('rm loop_sound'));
    await sleep(1000);
    const deleted = await evaluate('Module.boxAudio.stats()');
    console.log(`delete while playing: voices ${playing.voices} -> ${deleted.voices}, sounds ` +
      `${playing.sounds} -> ${deleted.sounds}`);
    expect(playing.voices === 1, 'the loop did not play');
    expect(deleted.voices === 0, 'the voice of the deleted sound still plays');
    expect(deleted.sounds === playing.sounds - 1, 'the worklet did not free the deleted sound');

    const errors = log.filter((l) => /error/i.test(l));
    const warnings = log.filter((l) => /box\.audio/.test(l));
    console.log(`errors: ${errors.length}, audio warnings: ${warnings.length}`);
    expect(!errors.length, 'the log has an error');
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
