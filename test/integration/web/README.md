# Browser checks

`shader_error.mjs` checks the shader error path of the wasm build in headless Chrome: it serves the built page on a free localhost port, breaks `/etc/shaders/mesh.wgsl` in the page's file system, expects one compile error with its line, no invalid WebGPU objects and a scene that keeps rendering, then restores the file. Build the page first and pass its folder (Chrome from `--chrome` or `$CHROME`, the macOS install by default; `--out <dir>` keeps the screenshots):

```sh
bake3 --target em --local-env=<name> apps/boxrun
node test/integration/web/shader_error.mjs \
    "$(tools/integration.py local-env --env <name>)/wasm32-Emscripten/debug/bin"
```

The page lands in `.bake/local_env/<name>/wasm32-Emscripten/debug/bin`, or in
`<output_dir>/local_env/<name>/...` when bake3's `bake.json` sets `output_dir`;
`tools/integration.py local-env --env <name>` prints the local env folder either way.

`transitions.mjs` checks transitions in the browser, where the render runs on the main thread:
it opens `test/integration/web/transitions.flecs`, which the page fetches from the same server
into its file system (`boxrun.html?preload=<path>`, see `apps/boxrun/shell.html`), at a fixed
delta of 1/60 s. The page opens a window only for a scene with a `Surface`, so the scene
declares one of 640x480 points and the check sets its viewport to that size. At frame 30 the
scene moves a square and changes a color, a fade and an emission, with transitions of 3 s. The
check takes screenshots until the square and the color reach their targets and passes when one
screenshot shows the square in between and one the color, the square never moves back, both end
at their targets and the log has no error and no invalid WebGPU object. It takes the same
arguments:

```sh
node test/integration/web/transitions.mjs \
    "$(tools/integration.py local-env --env <name>)/wasm32-Emscripten/debug/bin"
```

`transmission.mjs` checks that a rough transmissive shape is never dark in the browser before
`etc/shaders/transmission.wgsl` compiles, the case `Transmission_first_frame_not_dark` of
`test/gpu/render` simulates natively. It opens `test/integration/web/transmission.flecs` the same
way, at the same fixed delta and size: a bright, flat emissive wall fills the view, and at frame
30 the scene gives a tinted glass of roughness 1 and transmission factor 1 its box, which requests
the transmission shader. The browser delivers the compile outcome after the frame, so the frames
up to it sample the snapshot without the downsample pipeline. Before the page runs, the check
replaces `requestAnimationFrame` so that it runs the frames of the main loop one at a time, with a
screenshot after each, and wraps `createShaderModule`, `createRenderPipeline` and
`getCurrentTexture` of WebGPU to record in which frame the transmission shader was created, its
outcome arrived and its first pipeline was created, and which frame each screenshot shows. It
passes when at least one frame showed the glass before that pipeline existed, the center of the
glass is never black nor darker than its steady value (6 per channel), the image becomes steady
and the log has no error and no invalid WebGPU object. It prints the frames and the centers:

```sh
node test/integration/web/transmission.mjs \
    "$(tools/integration.py local-env --env <name>)/wasm32-Emscripten/debug/bin"
```

`particles.mjs` checks particle curves in the browser build, which read the curve table of the
particles from a storage buffer of the vertex stage. It opens `test/integration/web/particles.flecs`
the same way, at the same fixed delta and size: three jets rise, on the left one that cools from
white to red, in the middle one without `ParticleCurves` that stays white, on the right two with
equal curves that share their rows, from magenta to cyan. At frame 180 the scene changes the
gradient of the left jet to green and blue and gives six emitters out of view gradients of their
own, so the table grows and is created again. The check takes screenshots until the left jet
shows the new gradient and passes when an earlier screenshot showed the warm one there, the last
shows white in the middle and magenta and cyan on the right, and the log has no error and no
invalid WebGPU object. The frames before the browser compiled the simulation shader skip the
pools with an error, which the check ignores:

```sh
node test/integration/web/particles.mjs \
    "$(tools/integration.py local-env --env <name>)/wasm32-Emscripten/debug/bin"
```

`audio_core.mjs` checks the audio of the browser build (`docs/audio.md`): the mixer of the
audio core runs from its own wasm in an AudioWorklet, without shared memory, starts on a user
gesture and is driven by command batches over the port. It serves the page without COOP/COEP
headers, so `crossOriginIsolated` is false, and opens `test/integration/web/audio_core.flecs`,
a turning box. Chrome runs without an autoplay flag, so no audio starts without a gesture. Once
the scene draws, the check expects the audio context suspended before a gesture, frames drawn
for a second and no error in the log. A trusted click of the DevTools protocol
(`Input.dispatchMouseEvent`) on the canvas has to start it. The check then sends a sound of a
440 Hz sine at 0.25 (`Module.boxAudio.sound`) and a batch with a looped play command
(`Module.boxAudio.commands`, encoded with a `DataView` as in `src/audio/core/core.h`); an
`AnalyserNode` on the worklet node has to measure 440 Hz at a peak of 0.25 without a step
larger than the sine's. A volume command of 0.5 has to reach the output, the window of the
analyser holding the old peak at its start and 0.125 at its end, without a click. A 500 ms
busy loop on the main thread has to leave the clock and the quantum count of the worklet
advancing by its length with the voice playing and intact afterwards. A batch with an order
that has a cycle, a stop and a release has to come back as an `ORDER_REFUSED`, a `VOICE_END`
and a `SOUND_FREE` event, with the worklet holding no voice and no sound and the output silent.
The frames keep advancing and the log has no error, no `box.audio` warning and no invalid
WebGPU object. It prints its measurements, the events and the sizes of the core:

```sh
node test/integration/web/audio_core.mjs \
    "$(tools/integration.py local-env --env <name>)/wasm32-Emscripten/debug/bin"
```

`audio_core_safari.mjs` runs the same audio check in real Safari on macOS, driven by
`safaridriver` over the W3C WebDriver HTTP API (plain `node`, no npm dependency). It starts
`safaridriver -p <free port>`, opens a session, prints the Safari and macOS versions and stops
with exit status 2 when Safari has no WebGPU, when remote automation is off (run
`sudo safaridriver --enable` once as an admin and turn on "Allow remote automation" in the
Developer section of Safari Settings) or when no GUI session is logged in. Safari WebDriver has
no log endpoint, so the server injects a script at the top of the `<head>` of `boxrun.html`,
before the page runs, which records the console (`log`, `info`, `warn`, `error`), uncaught
errors and rejections, and counts the frames; the check reads that log from the page. It opens
`test/integration/web/audio_core.flecs`, expects the context suspended and no failure before a
gesture while the scene renders, then clicks the canvas with WebDriver Actions
(`POST /session/{id}/actions`, pointer down and up). If Safari does not take the click as a
gesture it tries a key press, and it prints `Module.boxAudio.gestures` and
`Module.boxAudio.resumes`, which tell an input that never reaches the page from a `resume()`
Safari refuses. Once running, it sends the sound of a 440 Hz sine at 0.25 and a looped play
command as `audio_core.mjs` does; the voice has to measure 440 Hz at a peak of 0.25, then 660 Hz
after a pitch command of 1.5, `Module._box_audio_web_apply` and `_box_audio_web_sound` have to be
exported, the frames keep advancing and the log has no error and no `box.audio` warning. It
leaves out the volume ramp, the main thread stall and the events of `audio_core.mjs`.
`--safaridriver` or `$SAFARIDRIVER` selects the driver, `/usr/bin/safaridriver` by default:

```sh
node test/integration/web/audio_core_safari.mjs \
    "$(tools/integration.py local-env --env <name>)/wasm32-Emscripten/debug/bin"
```

Before starting Chrome on macOS, the scripts check the `.app` bundle that contains the Chrome
executable (after resolving symlinks) for the `com.apple.quarantine` attribute
(`xattr -p com.apple.quarantine <app>`).
macOS holds a quarantined app, such as one installed by Homebrew, at its first launch check, and
no one can confirm the dialog of a headless session, so the scripts fail at once with exit status
2 and name the app instead of reporting "chrome did not start" after the connect timeout. Open
Chrome once from the Finder and confirm the dialog, run `xattr -dr com.apple.quarantine <app>`, or
pass `--chrome` with a copy without the attribute. Confirming the dialog keeps the attribute but
sets its user-approved flag (`0x0040` in the first field, e.g. `0081;...` becomes `00c1;...`); the
scripts accept such an approved app. The check is skipped off macOS, without `xattr`, and for a
Chrome outside a `.app` bundle. `quarantine.sh` checks this with a fake `Foo.app`, first
quarantined (the scripts fail at once), then approved (they go on to start it):

```sh
test/integration/web/quarantine.sh
```

When Chrome still hangs at the start ("chrome did not start", while `Google Chrome --version`
does not return either), look for another launch check holding it the same way.

`audio_module.mjs` checks the `box.audio` module in headless Chrome with
`test/integration/web/audio_module.flecs`, a scene that only uses the module and script: a
looping 440 Hz tone at a peak of 0.25 on a bus, and a click that `box.audio.play` plays at load.
It reads `AudioStatus` and runs commands through the shell of the page
(`Module.cwrap('box_command_web_run', 'string', ['string'])`). It expects
`AudioWaitingForGesture` before a trusted click and the click dropped, the tone at its level
after it, half the level after a script that the shell writes (`script_write`) sets
`AudioBus: {volume: 0.5}` on the bus, a suspended context (`AudioSuspended`) while the page is
hidden and a resumed one when it shows again, a one second `audio capture 1` in `/tmp` of the
page's file system with the tone, and the loop still playing after a 500 ms stall of the main
thread:

```sh
node test/integration/web/audio_module.mjs \
    "$(tools/integration.py local-env --env <name>)/wasm32-Emscripten/debug/bin"
```

`audio_spatial.mjs` checks spatial audio in headless Chrome with
`test/integration/web/audio_spatial.flecs`: a looping 440 Hz tone on an `AudioSpatial` source
heard from an `AudioListener` at the origin. After a trusted click it moves the source with
`script_write` from the left of the listener through its position to its right and beyond
`max_distance`, and measures the RMS of each channel of the output through a
`ChannelSplitter`: left louder on the left, both equal at the listener, right louder on the
right, the same power at every place (equal power) and no voice beyond `max_distance`:

```sh
node test/integration/web/audio_spatial.mjs \
    "$(tools/integration.py local-env --env audio)/wasm32-Emscripten/debug/bin"
```

`audio_reverb.mjs` checks the reverb with `test/integration/web/audio_reverb.flecs`, a bus
`world` that sends to the bus `hall` with `Reverb: {decay: 2.4}`. After a trusted click it
records `audio capture 4` while `box.audio.play` sends a click to the reverb, and measures the
decay of the tail after the click at the rate of the context. It then starts 32 looping voices
through the reverb and prints the load of the worklet, which `Module.boxAudio.bench` measures by
rendering 2000 quanta back to back, and which has to stay under real time:

```sh
node test/integration/web/audio_reverb.mjs \
    "$(tools/integration.py local-env --env <name>)/wasm32-Emscripten/debug/bin"
```

`audio_synth.mjs` checks synth sounds with `test/integration/web/audio_synth.flecs`. After a
trusted click it records `audio capture 2` while a `script_write` plays the prefab `Beep` (a
660 Hz sine at 0.5 with an envelope of 0.36 s) with `box.audio.play`, and measures its
frequency, its level and its length at the rate of the context. It then starts 64 looping voices
of `Hum` (a saw, a square and noise into a filter sweep) and prints the load of the worklet over
2000 quanta, which has to stay under real time:

```sh
node test/integration/web/audio_synth.mjs \
    "$(tools/integration.py local-env --env <name>)/wasm32-Emscripten/debug/bin"
```

`audio_sample.mjs` checks sound files (`SoundFile`, see `docs/audio.md`) in Chrome. It preloads
`test/integration/web/audio_sample.flecs` and the WAV and FLAC files of `test/assets/audio` with
`?preload=`; the scene decodes one second of 440 Hz at 44100 Hz. After a trusted click it
records two seconds (`audio capture 2`) while a `script_write` plays the sound once, and expects
440 Hz for one second at the actual rate of the context. It then sets the `SoundFile` of 2
seconds of stereo WAV and FLAC and prints the longest animation frame callback, the main
loop, while each decodes inline, and last deletes a sound whose looping source plays (`rm`) and
expects the worklet to drop the voice and free the sound:

```sh
node test/integration/web/audio_sample.mjs \
    "$(tools/integration.py local-env --env audio)/wasm32-Emscripten/debug/bin"
```

`audio_stream.mjs` checks music streams (`AudioStream`, see `docs/audio.md`) in Chrome with
`test/integration/web/audio_stream.flecs`, whose stream plays `test/assets/audio/tone.ogg`, 480 Hz
at a peak of 0.5 in a loop of 0.25 s. The server serves the Ogg files by their path and the page
preloads only the scene and `tone_960.ogg`, so the stream fetches `tone.ogg` next to the page.
After a trusted click it records two seconds (`audio capture 2`), which have to hold 480 Hz at 0.5
without a step larger than the sine's over the eight loop crossings and without a gap. It prints
`AudioStreamStatus.decode_ms`, the decode cost of a frame, and the longest animation frame, then
expects at least 1.5 s buffered in the worklet and no underrun, in the status and in the worklet,
after a 500 ms stall of the main thread. The file has to be fetched once and not be in the file
system of the page. A stream of a file served at no path has to fail with one error while the page
keeps drawing, and a stream of the preloaded `tone_960.ogg` has to play 960 Hz at 0.25 from the
page's file system without a new request:

```sh
node test/integration/web/audio_stream.mjs \
    "$(tools/integration.py local-env --env audio)/wasm32-Emscripten/debug/bin"
```

`audio_all_features.mjs` checks every part of `box.audio` together with
`test/assets/audio/all_features.flecs`: buses with a reverb send, synth clicks with a pitch
variance, a looping spatial synth drone on the left, music streamed from `tone.ogg`,
`sine_16.wav` played with `play_at` on the right and a capture at load. The page preloads the
scene and both files with `?preload=`. It expects the page to wait for a gesture, then after a
trusted click a level on both channels (an `AnalyserNode` per channel), a 500 ms stall of the
main thread with the worklet rendering on, no underrun of the stream and the frame count
advancing, the levels back after it, a hidden page suspended and a visible one resumed with its
levels, and a log without an error, an audio warning or an invalid WebGPU object. It prints the
longest and the mean animation frame before the gesture and while everything plays:

```sh
node test/integration/web/audio_all_features.mjs \
    "$(tools/integration.py local-env --env audio)/wasm32-Emscripten/debug/bin"
```
