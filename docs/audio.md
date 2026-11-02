# Audio

Box renders its sound in the audio core, `src/audio/core`: plain C without flecs, box or
platform code. The core is a mixer of voices and buses that renders blocks of float samples
into a left and a right channel. It is driven by a byte stream of commands and reports back
with events. Rendering does not allocate, lock, log or wait. Later tasks add the native output
and the `box.audio` module; this page describes the core and the web, the platform where audio
is the hardest to get right.

Native builds compile the core into box and do nothing else with it yet.

## The core

```c
box_audio_core_t* box_audio_core_new(const box_audio_core_desc_t *desc);
void box_audio_core_free(box_audio_core_t *core);
bool box_audio_core_sound(box_audio_core_t *core, uint32_t id, const float *data,
    int32_t frames, int32_t channels, float sample_rate);
void box_audio_core_apply(box_audio_core_t *core, const void *commands, int32_t size);
void box_audio_core_render(box_audio_core_t *core, float *left, float *right, int32_t frames);
int32_t box_audio_core_read_events(box_audio_core_t *core, box_audio_event_t *events, int32_t max);
```

`box_audio_core_new` is the only allocation, through the `alloc` and `free` hooks of the desc
(`malloc` and `free` on native when they are NULL). Every other call runs on the thread that
renders, between renders; `render` takes any frame count and mixes in blocks of
`BOX_AUDIO_CORE_BLOCK` (128) frames.

**Commands** are plain bytes without pointers, the same on every transport. Each command starts
with a header of a 16 bit type and a 16 bit size in bytes, header included, followed by 32 bit
fields in the byte order of the target (little endian everywhere box runs): the structs
`box_audio_cmd_*_t` of `core.h`. The `box_audio_write_*` functions write them into a buffer.
A command of an unknown type is skipped by its size and a too short one is ignored, both with
`BOX_AUDIO_EVENT_IGNORED`; a size that runs past the batch ends it. Commands take effect at the
start of the next render block. They are:

| Command | Fields |
|---------|--------|
| `PLAY` | voice handle, sound, bus, volume, pan, pitch, flags (`BOX_AUDIO_PLAY_LOOP`) |
| `STOP` | voice handle: the voice fades out over the ramp and ends |
| `VOICE_SET` | voice handle, `VOLUME`, `PAN`, `PITCH` or `BUS`, value |
| `BUS_SET` | bus, `VOLUME`, `PAN`, `MUTE` or `SEND` (the send level), value |
| `ORDER` | count, then per bus in processing order: bus, output, send |
| `SOUND_RELEASE` | sound: freed once no voice plays it |

The sender picks the voice handles, so it addresses a voice without waiting for a reply. Values
are clamped (volumes and send levels to 0..4, pans to -1..1, pitches to 1/64..16) and a NaN
becomes the low end.

**Sound memory belongs to the side that runs the core.** `box_audio_core_sound` gives the core
an id, a pointer in the core's own address space, a frame count, the channels (1, or 2
interleaved) and the sample rate of the sound. The transport fills it: on native the main thread
will allocate the frames and hand them to the render thread, on the web the worklet copies them
into the heap of its core. After a release, `BOX_AUDIO_EVENT_SOUND_FREE` tells the transport
when no voice uses the sound any more, so the side that owns it frees it, never the render path.

**Events** are `{ type, id, value }`: `VOICE_END` (id the handle, for an end, a stop or a
steal), `VOICE_STOLEN` (id the handle, value the steals so far), `SOUND_FREE`,
`ORDER_REFUSED` and `IGNORED` (id the command type). The core holds 256; newer ones are lost
when nobody reads them.

**Voices** are a fixed pool of `BOX_AUDIO_VOICE_MAX` (64) voices, plus 16 slots for voices that
fade out. A voice has a source, a bus, a volume, a pan and a pitch ratio. The source is a sample
buffer, mono or stereo, looped or not, read with linear interpolation; it is a tagged union in
`internal.h` with its functions in `source.c`, so the synth and music sources add a kind there.
A play when 64 voices play steals the one with the lowest level, the larger of its two gain
targets (the first of equals), with a fade of 2 ms. This is the voice cap: there is no
priority system.

**Buses** are a fixed pool of `BOX_AUDIO_BUS_MAX` (32); bus 0 is the master. Each has a volume,
a pan, a mute, an output and one send with a level, taken after its volume. An `ORDER` command
replaces the processing order and the routing as a whole. The core checks it: the master comes
last and outputs nowhere, every other bus outputs, and sends, only to buses after it, and no bus
appears twice. An invalid order is ignored with `ORDER_REFUSED`, so a cycle cannot hang the
render. A bus not in the order is silent and its ramps wait. The order starts as the master
alone.

**Ramps.** A voice and a bus each hold a gain per channel that combines volume, pan, mute and
fades; every change moves it linearly over `BOX_AUDIO_CORE_RAMP` (5 ms), and a voice starts at
0, so nothing clicks, a start or a stop included. A change of the bus of a voice or of the order
is immediate. The pan is equal power with unity at the center: the gains are
`sqrt(2) cos(a)` and `sqrt(2) sin(a)` for `a` from 0 to pi/2, so a centered mono voice plays
its samples as they are.

**The limiter** on the master keeps the peak at `BOX_AUDIO_LIMITER_THRESHOLD` (0.95): its gain,
one for both channels so the image does not shift, drops at once to the threshold over the peak
and rises back linearly over 100 ms. A signal under the threshold passes unchanged, and a final
clamp keeps the output within -1..1.

**Bad input.** Every sample a source reads and every sample a bus outputs that is NaN,
infinite, larger than 1e30 or smaller than 1e-30 becomes 0, so neither a NaN nor a denormal
reaches the output or stays in a bus, and a silent tail costs no more than sound.

**Determinism.** The ramps and the limiter advance per frame and the sums run in a fixed order,
so the same commands at the same block boundaries give bit identical output on the same build,
whether the frames are rendered one at a time or in one large block.

The tests are `test/cpu/audio`, which render offline; `bench/audio` mixes 64 voices through 8
buses and reports the time per frame.

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

The two talk only over the `MessagePort` of the `AudioWorkletNode`, with plain objects:

- `{ type: 'commands', data }`: a batch of commands of the core as an `ArrayBuffer`, which is
  transferred. The worklet copies it into the heap of its core and applies it between quanta.
- `{ type: 'sound', id, frames, channels, sampleRate, data }`: the frames of a sound as an
  `ArrayBuffer`, transferred. The worklet copies them into the heap of its core and keeps the
  pointer; the reply `{ type: 'sound', id, ok }` says whether the core took it.
- `{ type: 'events', events }` from the worklet: the events of the core after a quantum that
  had any. On `SOUND_FREE` the worklet has already freed the frames in its heap.
- `{ type: 'stats', id }` asks the worklet for its quantum count, clock, voices, sounds and
  memory, which the browser check uses.

In the page, `Module.boxAudio.commands(bytes)` sends a batch, `Module.boxAudio.sound(id,
samples, channels, rate)` a sound (a promise of whether the core took it),
`Module.boxAudio.events` keeps the last events and `Module.boxAudio.onevents` is called with
each new list; `Module.boxAudio.stats()` returns a promise of the stats. From C,
`box_audio_web_apply` and `box_audio_web_sound` of `src/audio/platform.h` copy the bytes out of
the main module and send them. Messages sent before the worklet has loaded wait in the page and
are sent once the node exists.

The heap of the core grows: allocating a batch or a sound can grow the wasm memory, which
detaches the `ArrayBuffer` of the old memory, so the worklet recreates its views of the channel
and event buffers after every allocation.

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
    -Wl,--initial-memory=1048576 -Wl,--max-memory=268435456 -Wl,--strip-all \
    -Wl,--export=box_audio_core_wasm_init ... command.c core.c source.c wasm.c \
    -o box_audio_core.wasm
```

into `box_audio_core.wasm` in the generated folder of the build: a freestanding module of a
few KB with its own memory, which starts at 1 MB and grows up to 256 MB, no imports (the core
needs no libm) and the exports of `src/audio/core/wasm.c`: `box_audio_core_wasm_init`, a first
fit heap with `_alloc` and `_free` that grows the memory, `_sound`, `_apply`, `_render`,
`_left`, `_right`, `_read_events`, `_events` and `_voices`. It is not built with `emcc`: a standalone wasm of `emcc` links
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

## Playing a sound by hand

Until the `box.audio` module exists, nothing in box sends commands. To hear the core, build the
page, serve its folder and open it with a scene that renders, then click the canvas:

```sh
bake3 --target em --local-env=audio apps/boxrun
BIN="$(tools/integration.py local-env --env audio)/wasm32-Emscripten/debug/bin"
mkdir -p "$BIN/test/integration/web"
cp test/integration/web/audio_core.flecs "$BIN/test/integration/web/"
python3 -m http.server 8080 -d "$BIN"
```

Open
`http://localhost:8080/boxrun.html?preload=/test/integration/web/audio_core.flecs#/test/integration/web/audio_core.flecs`
(`localhost` is a secure context, which `AudioWorklet` requires) and click the canvas. In the
developer console, a sound and a play command make a 440 Hz tone:

```js
const a = Module.boxAudio, rate = a.ctx.sampleRate, s = new Float32Array(rate);
for (let i = 0; i < rate; i++) s[i] = 0.25 * Math.sin(2 * Math.PI * 440 * i / rate);
await a.sound(1, s, 1, rate);
const v = new DataView(new ArrayBuffer(32));
v.setUint16(0, 1, true); v.setUint16(2, 32, true); v.setUint32(4, 1, true);
v.setUint32(8, 1, true); v.setFloat32(16, 1, true); v.setFloat32(24, 1, true);
v.setUint32(28, 1, true);
a.commands(v.buffer);
```

This is also the check for Safari, which the automated check cannot run: open the same address
in Safari on macOS, click the canvas, run the snippet and expect the tone and no `box.audio`
warning in the console.

The automated check of Chrome is `test/integration/web/audio_core.mjs`, see
`test/integration/web/README.md`.
