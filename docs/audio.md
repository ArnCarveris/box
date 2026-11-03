# Audio

Box renders its sound in the audio core, `src/audio/core`: plain C without flecs, box or
platform code. The core is a mixer of voices and buses that renders blocks of float samples
into a left and a right channel. It is driven by a byte stream of commands and reports back
with events. Rendering does not allocate, lock, log or wait. This page describes the
[`box.audio` module](#the-boxaudio-module) that apps use, the core, the native output and the
web, the platform where audio is the hardest to get right.

On macOS and Linux the core plays on a miniaudio device, see [Native output](#native-output).

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
| `REVERB` | slot, bus (or none), decay, damping, predelay, size, mix: see [Reverb](#reverb) |

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
`ORDER_REFUSED`, `IGNORED` (id the command type) and `LOST` (value the events lost). The core
holds `BOX_AUDIO_EVENT_MAX` (256) until `read_events`. The last slot is kept for `LOST`: when
the other 255 are full a newer event is dropped and counted in a single `LOST` event in that
slot, whose value grows with each further loss until it is read; reading it resets the count,
which `box_audio_core_events_lost` returns. `SOUND_FREE` is never lost, since the owner frees a
sound's memory only when it sees it. When it finds the ring full, the core marks the sound as
free pending (a state of its slot, so the id stays taken and no memory is needed: a sound is
pending at most once) and `read_events` turns the pending frees into events as soon as the ring
has room, in the same call or a later one. A burst of hundreds of plays at the cap in one batch,
each with a steal and an end, can therefore lose `VOICE_END` and `VOICE_STOLEN` events but not
the free of a sound released after it.

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

## Reverb

`src/audio/core/reverb.c` is an algorithmic reverb that runs on a bus: a feedback delay network
of `BOX_AUDIO_REVERB_LINES` (8) delay lines. A send bus with a reverb is the usual setup, so
sources reach it through the send of their bus.

- **The network.** The input, the mean of both channels, goes through a predelay line, then into
  every line with a sign pattern that is no row of the matrix, so the first reflection already
  spreads over all lines. The lengths are distinct primes, so mutually prime, from 39 to 98 ms
  at size 1, scaled down to a fifth at size 0. Each line has a one pole lowpass, the damping,
  and a gain `10^(-3 L / (decay x rate))`, so every line loses 60 dB in `decay` seconds whatever
  its length `L`. The feedback matrix is the 8 x 8 Hadamard matrix over `sqrt(8)`, orthogonal.
  The left and the right outputs take the lines with two orthogonal sign patterns, which keeps
  the channels decorrelated.
- **It cannot blow up.** The matrix keeps the energy, the lowpass has a gain of at most 1 and
  each gain is at most `0.9999`, so the loop gain stays under unity. A `REVERB` command is
  clamped: decay to `BOX_AUDIO_REVERB_DECAY_MIN..MAX` (0.05..60 s), damping, size and mix to
  0..1 and predelay to 0..`BOX_AUDIO_REVERB_PREDELAY_MAX` (0.5 s), a NaN to the low end. Every
  value a line keeps is flushed like a bus sample, so no denormal forms in a tail.
- **Mix.** The bus outputs its signal at `1 - mix` and the reverb at `mix`; 1, the default, is
  the fully wet send bus.
- **Changes.** Decay and damping move to a new value over about 50 ms, a block at a time, and
  mix ramps over 20 ms. A change of size or predelay would change the lengths of the lines,
  which clicks: the wet signal fades out over 50 ms, the lines are cleared and take their new
  lengths, and it fades back in over 50 ms (`BOX_AUDIO_REVERB_FADE`, 100 ms in all), while the
  input fades into the clear lines over the same 50 ms, so a steady sound does not enter them
  with a step. A change of the bus does the same.
- **Silence costs nothing.** The tail rings out after its sources stop. Once the input has been
  silent for longer than the predelay plus the longest line, and every line has read out under
  -140 dB for a whole length of the longest line, so nothing louder is left in them, the reverb
  clears its lines and stops; while it is stopped and no signal comes in, a block costs a scan
  of the input for a sample over 1e-9. `box_audio_core_reverb_running` tells whether it runs.
- **Taking it off.** A `REVERB` with bus `BOX_AUDIO_BUS_NONE` takes the reverb off its bus at
  once: it gets no more input and its tail fades out over 50 ms into the master, so deleting a
  reverb bus does not cut the tail.
- **Memory.** The delay memory belongs to the side that runs the core, like sound memory:
  `box_audio_reverb_floats(rate)` floats per slot (about 200 KB at 48 kHz), sized for the
  largest size and predelay at the rate. `box_audio_core_reverb_memory` gives it to a slot once,
  and the core clears it; the render path never allocates. Until a slot has memory a reverb on
  it does not run and its bus stays dry. The transports allocate it on the `reverb` call of the
  transport interface: the null transport on the main thread, the native one on the main thread
  and hands it to the audio thread as a message of the command ring, the web one with a
  `{ type: 'reverb', slot }` message, on which the worklet allocates it in the heap of its core.
  Each frees it after its core.

`BOX_AUDIO_REVERB_MAX` (1) reverbs run at once; the slots are an array, so more only cost their
memory and their time. Convolution is not used, since it needs recorded impulse responses.

The tests are the `Reverb` suite of `test/cpu/audio`, from an impulse in an offline render:
the decay time at 44.1 and 48 kHz, damping, predelay, stereo correlation, density, stability at
extreme values, silence, changes without clicks, sends, the second reverb, a delete and no
allocation. `bench/audio` has a `Reverb` suite in ns per frame, and
`test/integration/web/audio_reverb.mjs` measures a tail and the load of the worklet in Chrome.

## Transports

The main thread reaches the core through a transport, one small interface in
`src/audio/transport.h` that every output implements:

```c
bool commands(t, data, size);                      // send a command batch, copied
bool sound(t, id, data, frames, channels, rate);   // hand over a sound, copied
int32_t read_events(t, events, max);               // the events of the core, oldest first
box_audio_state_t state(t);                        // what the output does
void update(t, delta);                             // once per frame
void stats(t, stats);                              // voices and load of the core
bool capture(t, frames);                           // record the next frames the core renders
float* capture_take(t, &frames);                   // a finished capture, NULL while it records
```

| Transport | Where the core runs |
|-----------|---------------------|
| native | on the audio thread of a miniaudio device, macOS and Linux |
| web | in the AudioWorklet of the page, over its `MessagePort` |
| null | on the main thread, which renders the frames of each frame's delta |

The state is `OFF` (no output), `NO_DEVICE` (the null transport renders), `STARTING` (the web
loads the worklet), `WAITING` (the page waits for a gesture), `RUNNING` (a device plays),
`LOST` (the device disappeared and the main thread renders until it reopens), `FAILED` (the page
could not start its audio) or `SUSPENDED` (the page is hidden).

**Captures** tap the core where it renders, so they work with every transport: the null
transport copies the frames it renders on the main thread; the native transport hands the audio
thread a buffer through the command ring, which the audio thread fills after each block and
reports full with an atomic flag, so the main thread takes it back without a lock; the worklet
records its quanta and posts the frames back over the port. The load is the time a render takes
over the time it renders, smoothed; the native audio thread and the null transport measure it,
the worklet does not (it has no clock finer than a millisecond) and reports 0. Every transport frees the frames of
a sound itself after `SOUND_FREE`; the main thread keeps the last 4096 events until
`read_events`.

**Where the output opens.** A world that imports box opens nothing: thousands of tests import
it and must not touch audio hardware. `box_app_run` opens the output of its world
(`src/audio/output.c`), as the singleton `box.app.AudioTransport`, which the system
`AudioTransportUpdate` updates in `OnStore` with the delta of the frame. Every open gets a new
serial, which tells `box.audio` to send its sounds and state again:

- a real app opens the default device, and the null transport when there is none;
- `--headless` opens no device and runs the null transport;
- `--no-audio` opens nothing, and the state is `OFF`.

**No device is not an error.** The pm containers and CI have no audio device. A failed open
logs one warning, `box.audio: no audio device (...), the app runs without sound`, never an
error, because the e2e runs fail on a logged error, and the app goes on with the null
transport and the state `NO_DEVICE`.

**The null transport** renders `floor(time x rate)` frames in total, where time is the sum of
the frame deltas, at most one second per frame. With a fixed `--delta-time` every run renders
the same frames at the same block boundaries, so the audio is bit identical in every run, which
the capture of the audio module depends on. Commands apply at once and take effect at the
start of the next render.

## Native output

`src/audio/native.c` is the only file that includes miniaudio, bundled header only in
`project.json` (tag `0.11.26`) for macOS and Linux. It uses the device API alone, built with
`MA_NO_ENGINE`, `MA_NO_NODE_GRAPH`, `MA_NO_RESOURCE_MANAGER`, `MA_NO_DECODING`,
`MA_NO_ENCODING`, `MA_NO_GENERATION` and `MA_NO_NULL` (so a machine without audio fails the
open instead of playing into the silent null backend of miniaudio). Mixing, panning and
resampling are the core's. The device is opened for 32 bit float stereo at the native rate of
the device, which becomes the rate of the core, with `noFixedSizedCallback`, so the callback
renders any frame count the device asks for, in blocks of 512 frames.

**Two rings.** The main thread and the device callback share two single producer single
consumer rings of bytes (`src/audio/ring.c`) with C11 atomics: commands and sounds to the audio
thread (64 KB), events back (4096 events). Each side owns one index, loads the other with
acquire and stores its own with release; a write is all or nothing. `src/render/ring.c` is not
a model for them: it takes a lock.

- A command batch is split at command boundaries into messages of at most 4 KB. A sound is a
  message with a pointer to frames the main thread copied and owns.
- The messages first go to a queue of the main thread, which writes as many as fit into the
  ring. A full ring keeps the rest on the main thread for the next frame; neither side blocks.
- The callback takes every message the ring holds, applies it, renders, and moves the events
  of the core into the event ring as far as it has room; the rest wait in the core. A sound the
  core refuses comes back as an internal event, so the main thread frees it. A sound waits in
  the ring while the event ring has no room for that refusal.
- The callback never allocates, locks, logs or waits.

**Thread priority.** The audio thread gets a real time priority through miniaudio
(`ma_context_config.threadPriority`) and nowhere else. It counts against
`BOX_JOBS_RESERVED_CORES`, which keeps 3 cores free: the main thread, the render thread and the
audio thread; the job pool takes the rest.

**A device that disappears**, such as unplugged headphones, does not stop the app. miniaudio
follows a change of the default device itself where the backend supports it (reported as
`rerouted`). A stop the main thread did not ask for marks the device lost: the next frame
closes it, which joins its thread, logs one warning, and the main thread takes over the rings
and renders the deltas like the null transport, so voices end and sounds are freed. Every 2
seconds it tries to reopen the default device at the rate of the core; miniaudio converts when
the new device has another rate.

**Linking.** Runtime linking stays on: miniaudio loads the libraries of its backends
(PulseAudio, ALSA, JACK on Linux; CoreAudio on macOS) at run time, so there are no new link
flags. A notarized macOS build, whose hardened runtime may refuse to load them, builds
`native.c` with `-DMA_NO_RUNTIME_LINKING` and links `-framework CoreFoundation -framework
CoreAudio -framework AudioToolbox`, or keeps runtime linking and signs with the entitlements
the miniaudio documentation lists for macOS.

The web does not compile the device part: the bundle is not fetched for Emscripten and
`native.c` compiles to a stub there.

The tests are `Ring`, `Transport` and `NoDevice` in `test/cpu/audio`, which need no device.

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
  had any. The worklet reads until the core has none left, 64 per call, so no backlog builds
  up across quanta. On `SOUND_FREE` the worklet has already freed the frames in its heap.
- `{ type: 'stats', id }` asks the worklet for its quantum count, clock, voices, sounds,
  memory and the events the core lost since the last `LOST` was read, which the browser check
  uses.

The web transport of `transport.c` sends with these functions and reads the events from
`Module.boxAudio.queue`, which keeps up to 4096 events until the main module takes them.

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
fit heap with `_alloc` and `_free` that grows the memory, `_sound`, `_reverb_floats`,
`_reverb`, `_apply`, `_render`, `_left`, `_right`, `_read_events`, `_events`, `_events_lost` and `_voices`. It is not built
with `emcc`: a standalone wasm of `emcc` links start code and libc from the Emscripten cache,
which the core does not use, and building those for a new configuration fails when the cache
folder is a symlink. The script writes the bytes as
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
app opens its audio output, before the first frame, so that the sounds and the commands of that
frame reach the worklet (they wait in the page while it loads). The key and mouse button
handlers of the canvas
(`box_web_callbacks` in `src/input/platform_web.c`) resume it on the first key or button event.
Until then the app runs normally, silently, and logs nothing; `AudioStatus.state` is
`AudioWaitingForGesture`, and `box.audio` drops the plays that do not loop (see
[Before the gesture](#before-the-gesture)).

**A hidden page** gets no animation frames, so no commands arrive and looping sounds would never
end. On `visibilitychange` to hidden the page ramps a gain node after the worklet to 0 over
50 ms and then suspends the context (`AudioSuspended`); when it shows again it resumes the
context, if it was running, and ramps the gain back to 1. `Module.boxAudio.suspends` and
`visibilityResumes` count both. Native apps keep playing when they lose the focus.

A failure to create audio, a browser without Web Audio or without `AudioWorklet`, a worklet that
does not load or a core that does not start in it, logs one warning
(`box.audio: ..., the app runs without sound`) and the app keeps running without sound.

## Playing a sound by hand

Apps play through the `box.audio` module. To hear the core alone, build the page, serve its
folder and open it with a scene that renders, then click the canvas:

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

The automated check of Chrome is `test/integration/web/audio_core.mjs`, and of Safari
`test/integration/web/audio_core_safari.mjs`, which drives real Safari through `safaridriver`
on macOS (see `test/integration/web/README.md`):

```sh
node test/integration/web/audio_core_safari.mjs "$BIN"
```

Safari automation has to be enabled once on the host, by an admin, with
`sudo safaridriver --enable` and "Allow remote automation" in the Developer section of Safari
Settings (Advanced > "Show features for web developers" shows it), and needs a logged-in GUI
session; without it the check stops with exit status 2 and says so. Where it cannot run, check
Safari by hand: open the same address in Safari on macOS, click the canvas, run the snippet and
expect the tone and no `box.audio` warning in the console.

`Module.boxAudio.gestures` counts the key and mouse button events that reached the audio and
`Module.boxAudio.resumes` the calls of `ctx.resume()` they made, which tells an input that never
arrives from a resume the browser refuses.

## The box.audio module

`include/box/audio.h`, in `src/audio/audio.c` and `src/audio/sync.c`, makes audio data driven:
components that a script, the REST API or the `set` command write, sent to the core once per
frame, the same on every platform.

| Component | What it is |
|-----------|------------|
| `AudioPcm` | The samples of a sound (`samples`, `channels`, `sample_rate`, 0 for the output rate); `box_audio_sound_from_pcm` sets it from C. Later tasks add other sound kinds |
| `AudioBus` | A mixer bus: `output` (0 or `master` for the master), `volume` (1), `pan`, `mute`, `send` and `send_level` |
| `Reverb` | A reverb on the bus of the entity: `decay` (1.5 s), `damping` (0.5), `predelay` (0.02 s), `size` (0.5), `mix` (1) |
| `AudioOutput` | The bus a source, or a sound by default, plays on |
| `AudioSource` | A sound that plays while the entity exists: `sound`, `volume` (1), `pitch` (1), `loop`, `playing` (true) |
| `AudioSettings` | Singleton: `master_volume` (1) and `mute` |
| `AudioStatus` | Singleton, read only: `state`, `sample_rate`, `voices`, `voices_stolen`, `load`, `buses` |

Constructors give the defaults in parentheses, so `AudioSource: {sound: click}` in a script plays
at unity volume and pitch. From C, `ecs_set` writes every member; use `ecs_ensure` to keep the
defaults.

```
music { AudioBus: {volume: 0.5} }
sfx { AudioBus: {output: music} }
hum { AudioSource: {sound: hum_pcm, loop: true}  AudioOutput: {bus: sfx} }
const played: bool = box.audio.play(click, 0.8)
```

`box_audio_play(world, sound, volume)`, in scripts `box.audio.play(sound, volume)`, plays a
sound once on the bus of its `AudioOutput`, without an entity per play; there is no control of a
voice after it started. `box_audio_capture(world, seconds, path)`, in scripts
`box.audio.capture(seconds, path)`, records the master output to a WAV file.

**One batch per frame.** The system `AudioSync` runs in `PreStore`, after the transforms and
before the output renders in `OnStore`. It remembers what it sent and writes a command only for
what differs, all into one batch, and sends nothing in a frame without changes. Its order:
the events of the output (ended voices, steals, freed sounds), the buses, the order, the master
settings, new sounds, the sources, then the plays, the stops of deleted sources and the releases
of sounds. A world without audio components pays the empty queries.

**Buses** get an index of the core, up to 31 besides the master; more log one error and are
silent. The order is compiled whenever a bus appears, disappears or changes its output or send:
a depth first walk computes the longest path of each bus to the master and finds cycles; the
deepest buses come first, the master last. A cycle through outputs and sends logs one error that
names the buses on it and keeps the last valid order, which the core keeps playing. A deleted
bus, or an output or send that names no bus, routes to the master: its sources and one shots are
moved to the master and the index is reused a frame later. A Disabled bus is muted.

**Reverbs.** A scene sets its reverb by having an entity with `AudioBus` and `Reverb`, and
unloading the scene deletes it, which fades its tail out:

```
hall { AudioBus: {output: master}; Reverb: {decay: 2.4, damping: 0.4, predelay: 0.02} }
world { AudioBus: {output: master, send: hall, send_level: 0.3} }
```

Components on one line are separated by `;` (or a newline): flecs script does not take a comma
there. `master` is `box.audio.master`, an entity without components that names the master. A reverb
gets a slot of the core and the module asks the transport for its memory before the first
`REVERB` command; it sends the parameters again when they change. One reverb runs at once: a
second one logs one error that names its entity (`box.audio: more than 1 reverb, '...' stays
dry`) and its bus stays dry, until the first one is deleted. Reverb zones, rooms from geometry
and other effects are later work.

**Sources** start a voice when `playing` is true and their sound is known, and stop it when
`playing` goes false, the sound or `loop` changes or the entity is deleted, which fades it out
over the ramp of the core. A sound that does not loop sets `playing` to false when it ends. A
Disabled source keeps its voice at volume 0. Prefabs do not play; their instances get their own
copy of `AudioSource` and play on their own, as `ParticleEmitter` instances do.

**Sounds** are sent to the output once, with an id of the core; a change of `AudioPcm` releases
the old id and sends the new samples, and a deleted sound is released, freed by the side that
owns the frames once no voice plays it.

### Before the gesture

A scene plays the same on native, on the web and headless. On the web, plays requested while the
page waits for a gesture are dropped unless they loop: a `box.audio.play` and the start of a
source that does not loop are skipped (the source's `playing` goes false), while a looping source
starts and is heard from the gesture on. So a page does not burst out with every click sound of
its first seconds at the first key press. Native and headless runs have no gesture and drop
nothing.

### The audio command and captures

The shell command `audio` (`src/command/cmd_audio.c`) prints the state, the rate, the voices, the
steals and the load, then the master and the buses in processing order, with the values of the
reverb of a bus and `(dry, every slot taken)` for a reverb that has no slot. `audio volume <v>` and
`audio mute [on|off]` write `AudioSettings`. `audio capture <seconds>` records the master output
to a 16 bit stereo WAV resource of the session, fetched like a `capture` PNG with
`GET /shell/session/<id>/resource/<name>`, empty until the file is written in one rename. In the
browser it writes `/tmp/audio_capture_<n>.wav` in the file system of the page and returns the
path; the page runs commands with `Module.cwrap('box_command_web_run', 'string', ['string'])`.
Headless with a fixed `--delta-time`, two runs write identical captures, which is how agents and
tests inspect audio and how a render can be listened to. `box.perf` reports the largest load of
a run as `PerfStatus.audio_load` and `audio_load` in its report.

The tests are `Module`, `Source`, `Script` and `Capture` in `test/cpu/audio`, through the null
transport; `test/integration/e2e/audio.py` drives the command on a headless app, and
`test/integration/web/audio_module.mjs` checks the gesture, the levels, a hidden page, a capture
and a stall in Chrome with `test/integration/web/audio_module.flecs`:

```sh
node test/integration/web/audio_module.mjs \
    "$(tools/integration.py local-env --env audio)/wasm32-Emscripten/debug/bin"
```
