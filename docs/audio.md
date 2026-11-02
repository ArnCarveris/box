# Audio

Box renders its sound in the audio core, `src/audio/core`: plain C without flecs, box or
platform code. The core is created with a sample rate and renders blocks of float samples
into a left and a right channel. Rendering does not allocate, lock or log. For now it plays one
sine tone with a frequency and a peak level; later tasks grow it into the mixer and add the
native output and the `box.audio` module.

Native builds compile the core into box and do nothing else with it yet. This page describes
the web, the platform where audio is the hardest to get right.

## Two threads on the web

The web build has no threads: there is no render thread and jobs run inline, so the main thread
runs every frame. Audio rendered on the main thread would glitch on every frame stall. The core
therefore runs in an AudioWorklet, on the audio rendering thread of the browser:

- **Main thread**: the main wasm module (box, flecs, the app). `src/audio/platform_web.c`
  creates the `AudioContext`, loads the worklet, starts the core with `AudioContext.sampleRate`
  (44100 and 48000 both occur, it never assumes one) and sends commands to it.
- **Audio thread**: an `AudioWorkletProcessor` that owns a second, small wasm instance of the
  core. Every `process` call renders one quantum of 128 frames into static buffers of the
  core's memory and copies them to the outputs.

The two talk only over the `MessagePort` of the `AudioWorkletNode`. Commands are plain objects,
`{ type: 'tone', frequency, level }` today; `{ type: 'stats', id }` asks the worklet for its
quantum count and clock (`Module.boxAudio.stats()` returns a promise of the reply), which the
browser check uses. Commands sent before the worklet has loaded wait in the page and are sent
once the node exists.

A stall of the main thread does not stop the worklet: the browser keeps calling `process`, and
only the commands of the stalled frames arrive late.

## Why no shared memory

Emscripten can run a worklet on the shared memory of the main module (`-sAUDIO_WORKLET
-sWASM_WORKERS`), but that does not link with this repository today. With emcc 6.0.8 against the
wasm `libresvg.a`:

- without `-pthread`: `undefined symbol: pthread_mutex_destroy` and more, from the Rust `std` in
  `libresvg.a`;
- with `-pthread`: `--shared-memory is disallowed by resvg-...rcgu.o because it was not compiled
  with 'atomics' or 'bulk-memory' features.`

Fixing that takes a nightly Rust with `-Zbuild-std` in bake3 and on every runner, and shared
memory requires COOP/COEP headers on every host that serves a box page. A separate module needs
neither: the page works with `crossOriginIsolated` false and without `SharedArrayBuffer`.

miniaudio is not used on the web: its web backend forbids `-std=c*` flags, while box sets `c11`,
and its worklet mode needs `-sASYNCIFY` on the whole binary.

## How the core is built and delivered

The `rules` of `project.json` run `tools/audio_core.py` for `src/audio/core/core.c` before box
compiles, on every build of box. For a target other than Emscripten the script does nothing. For
`--target em` it compiles the C files of `src/audio/core` with the clang of Emscripten (the
`LLVM_ROOT` of `em-config`):

```sh
clang --target=wasm32-unknown-unknown -O2 -msimd128 -std=c11 -ffreestanding -nostdlib \
    -DBOX_AUDIO_CORE_WASM -Wl,--no-entry -Wl,-z,stack-size=65536 \
    -Wl,--initial-memory=1048576 -Wl,--max-memory=1048576 -Wl,--strip-all \
    -Wl,--export=box_audio_core_wasm_init ... core.c wasm.c -o box_audio_core.wasm
```

into `box_audio_core.wasm` in the generated folder of the build: a freestanding module of about
1 KB with its own memory, no imports (the sine needs no libm) and the exports of
`src/audio/core/wasm.c` (`box_audio_core_wasm_init`, `_render`, `_left`, `_right`,
`_set_frequency`, `_set_level`). It is not built with `emcc`: a standalone wasm of `emcc` links
start code and libc from the Emscripten cache, which the core does not use, and building those
for a new configuration fails when the cache folder is a symlink. The script writes the bytes as
a C array to `.bake/generated/box_audio_core_wasm.h` in the box folder, which
`src/audio/platform_web.c` includes; bake3 puts the generated folder of a build on the include
path of test harnesses only, so the header needs a fixed path. The core is built with `-O2` in
every configuration and the header is rewritten only when its bytes change, so the build is the
same for every local env; a stamp skips the compile while the sources are older than it.

So the core travels inside the main module and the page needs no extra file: every app that
uses box gets it without setup, whatever its shell. At start, `platform_web.c` copies the bytes
into an `ArrayBuffer`, makes the worklet script from a Blob URL and creates the node with the
bytes and the sample rate in `processorOptions`. The processor compiles the bytes synchronously
in its constructor (`new WebAssembly.Module`, then `new WebAssembly.Instance`), before any sound
plays. Bytes rather than a compiled `WebAssembly.Module` are passed because they clone into a
worklet in every browser, Safari included.

Nothing is committed in binary form: a clean tree builds the page with

```sh
bake3 --target em --local-env=<name> apps/boxrun
```

## Starting and failing

Browsers only start audio after a user gesture. The context is created suspended when the
canvas gets its input backend, and the key and mouse button handlers of the canvas
(`box_web_callbacks` in `src/input/platform_web.c`) resume it on the first key or button event.
Until then the app runs normally, silently, and logs nothing.

A failure to create audio, a browser without Web Audio or without `AudioWorklet`, a worklet that
does not load or a core that does not start in it, logs one warning
(`box.audio: ..., the app runs without sound`) and the app keeps running without sound.

## The temporary tone

Until the `box.audio` module exists, `?tone=<hz>` in the address of a page plays a tone of that
frequency at a peak of 0.25 once the context runs, and `Module._box_audio_web_tone(hz, level)`,
a C function exported to the page, sends a new tone command from C.

To open the tone page by hand, build the page, serve its folder and open it with the tone and a
scene that renders, then click the canvas:

```sh
bake3 --target em --local-env=audio apps/boxrun
BIN="$(tools/integration.py local-env --env audio)/wasm32-Emscripten/debug/bin"
mkdir -p "$BIN/test/integration/web"
cp test/integration/web/audio_tone.flecs "$BIN/test/integration/web/"
python3 -m http.server 8080 -d "$BIN"
```

Then open
`http://localhost:8080/boxrun.html?preload=/test/integration/web/audio_tone.flecs&tone=440#/test/integration/web/audio_tone.flecs`
(`localhost` is a secure context, which `AudioWorklet` requires), click the canvas and hear a
440 Hz tone while the box turns. In the developer console,
`Module._box_audio_web_tone(660, 0.25)` changes the tone, and `Module.boxAudio.ctx.state` shows
the state of the context.

The automated check of Chrome is `test/integration/web/audio_tone.mjs`, and of Safari
`test/integration/web/audio_tone_safari.mjs`, which drives real Safari through `safaridriver`
on macOS (see `test/integration/web/README.md`):

```sh
node test/integration/web/audio_tone_safari.mjs "$BIN"
```

Safari automation has to be enabled once on the host, by an admin, with
`sudo safaridriver --enable` and "Allow remote automation" in the Developer section of Safari
Settings (Advanced > "Show features for web developers" shows it), and needs a logged-in GUI
session; without it the check stops with exit status 2 and says so. Where it cannot run, check
Safari by hand: open the same address in Safari on macOS, click the canvas, and expect the tone,
the frequency change from the console and no `box.audio` warning in the console.

`Module.boxAudio.gestures` counts the key and mouse button events that reached the audio and
`Module.boxAudio.resumes` the calls of `ctx.resume()` they made, which tells an input that never
arrives from a resume the browser refuses.
