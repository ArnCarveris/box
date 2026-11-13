#!/usr/bin/env node
import { spawn, spawnSync } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdtempSync, readFileSync, realpathSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const USAGE = `Checks spatial audio in headless Chrome with test/integration/web/audio_spatial.flecs:
a looping 440 Hz tone at a peak of 0.25 on a Spatial source, heard from a Listener
at the origin.

  node test/integration/web/audio_spatial.mjs [--chrome <path>] <bin dir>

<bin dir> holds boxrun.html, .js and .wasm (see audio_core.mjs). After a trusted click starts
the audio, the check moves the source from the left of the listener (x -10) through the center
(x 0, at the listener) to its right (x 10) with script_write, and measures the level of each
channel of the output through a ChannelSplitter: the left channel has to be louder on the left,
both equal in the center, the right louder on the right, with the power of both channels the
same at each place (equal power), and a source beyond max_distance has to give its voice back.
The log has no error and no audio warning. Exits 0 on a pass, 1 on a failure and 2 when Chrome
or the page cannot start.`;

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const SCENE = '/test/integration/web/audio_spatial.flecs';
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
const CHROME_DEFAULT = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const LEVEL = 0.25;
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

// Taps the worklet node once with a ChannelSplitter and an AnalyserNode per channel, then
// measures the RMS of each channel over the last 8192 frames
const MEASURE = `(async () => {
  const audio = Module.boxAudio;
  if (!window.boxTaps) {
    const split = audio.ctx.createChannelSplitter(2);
    window.boxTaps = [audio.ctx.createAnalyser(), audio.ctx.createAnalyser()];
    audio.node.connect(split);
    window.boxTaps.forEach((tap, i) => { tap.fftSize = 8192; split.connect(tap, i); });
    await new Promise((r) => setTimeout(r, 1000));
  }
  const rms = (tap) => {
    const data = new Float32Array(tap.fftSize);
    tap.getFloatTimeDomainData(data);
    let sum = 0;
    for (let i = 0; i < data.length; i++) sum += data[i] * data[i];
    return Math.sqrt(sum / data.length);
  };
  return { left: rms(window.boxTaps[0]), right: rms(window.boxTaps[1]) };
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
    // The shell gets whole components, the member is taken here: a JSON string as get prints it
    const audioStatus = async (member) => {
      const out = await evaluate(shell('get $ box.audio.Status'));
      try {
        return JSON.stringify(JSON.parse(out).Status[member]);
      } catch {
        return out;
      }
    };

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
    const runningBy = Date.now() + 3000;
    while (Date.now() < runningBy && await audioStatus('state') !== '"Running"') {
      await sleep(100);
    }
    expect(await audioStatus('state') === '"Running"',
      'Status is not Running after the gesture');

    // The RMS of a sine of peak LEVEL, centered, in each channel
    const center = LEVEL / Math.SQRT2;
    const place = async (x, z = 0) => {
      const code = `using box.transform\nsource {\n  Position: {${x}, 0, ${z}}\n}\n`;
      await evaluate(shell(`script_write move ${JSON.stringify(code)}`));
      await sleep(600);
      const level = await evaluate(MEASURE);
      const voices = (await evaluate('Module.boxAudio.stats()')).voices;
      console.log(`source at x ${x} z ${z}: left ${level.left.toFixed(4)}, right ` +
        `${level.right.toFixed(4)}, voices ${voices}`);
      return { ...level, voices };
    };
    const power = (l) => l.left * l.left + l.right * l.right;
    const left = await place(-10);
    expect(left.left > center * 1.3 && left.right < center * 0.1,
      'a source on the left is not in the left channel');
    expect(Math.abs(power(left) - 2 * center * center) < 0.1 * 2 * center * center,
      'the pan on the left is not equal power');
    const middle = await place(0);
    expect(Math.abs(middle.left - center) < 0.01 && Math.abs(middle.right - center) < 0.01,
      'a source at the listener is not centered at full volume');
    const half = await place(5, -5);
    expect(half.right > half.left && Math.abs(power(half) - 2 * center * center) <
      0.1 * 2 * center * center, 'a source to the right front is not equal power');
    const right = await place(10);
    expect(right.right > center * 1.3 && right.left < center * 0.1,
      'a source on the right is not in the right channel');
    expect(right.voices === 1, 'the source does not play one voice');
    const away = await place(1000);
    expect(away.left < 0.001 && away.right < 0.001, 'a source beyond max_distance is heard');
    expect(away.voices === 0, 'a looping source beyond max_distance keeps its voice');
    const back = await place(-10);
    expect(back.left > center * 1.3 && back.voices === 1,
      'the source did not come back in range');

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
