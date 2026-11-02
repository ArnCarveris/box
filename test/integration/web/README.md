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

`audio_tone.mjs` checks the audio of the browser build (`docs/audio.md`): the audio core runs
from its own wasm in an AudioWorklet, without shared memory, and starts on a user gesture. It
serves the page without COOP/COEP headers, so `crossOriginIsolated` is false, and opens
`test/integration/web/audio_tone.flecs`, a turning box, with `?tone=440`, the temporary trigger
of the tone. Chrome runs without an autoplay flag, so no audio starts without a gesture. Once
the scene draws, the check expects the audio context suspended before a gesture, frames drawn
for a second and no error in the log. A trusted click of the DevTools protocol
(`Input.dispatchMouseEvent`) on the canvas has to start it; an `AnalyserNode` on the
worklet node then has to measure 440 Hz at a peak of 0.25. A 500 ms busy loop on the main thread
has to leave the clock and the quantum count of the worklet advancing by its length, with the
tone intact afterwards, and `Module._box_audio_web_tone(660, 0.25)`, a command sent from C, has
to change the frequency to 660 Hz. The frames keep advancing and the log has no error, no
`box.audio` warning and no invalid WebGPU object. It prints its measurements and the size of the
core:

```sh
node test/integration/web/audio_tone.mjs \
    "$(tools/integration.py local-env --env <name>)/wasm32-Emscripten/debug/bin"
```

`audio_tone_safari.mjs` runs the same audio check in real Safari on macOS, driven by
`safaridriver` over the W3C WebDriver HTTP API (plain `node`, no npm dependency). It starts
`safaridriver -p <free port>`, opens a session, prints the Safari and macOS versions and stops
with exit status 2 when Safari has no WebGPU, when remote automation is off (run
`sudo safaridriver --enable` once as an admin and turn on "Allow remote automation" in the
Developer section of Safari Settings) or when no GUI session is logged in. Safari WebDriver has no log endpoint, so the
server injects a script at the top of the `<head>` of `boxrun.html`, before the page runs, which
records the console (`log`, `info`, `warn`, `error`), uncaught errors and rejections, and counts
the frames; the check reads that log from the page. It expects the context suspended and no
failure before a gesture while the scene renders, then clicks the canvas with WebDriver Actions
(`POST /session/{id}/actions`, pointer down and up). If Safari does not take the click as a
gesture it tries a key press, and it prints `Module.boxAudio.gestures` and
`Module.boxAudio.resumes`, which tell an input that never reaches the page from a `resume()`
Safari refuses. Once running, the tone has to measure 440 Hz at a peak of 0.25, then 660 Hz after
`Module._box_audio_web_tone(660, 0.25)`, the frames keep advancing and the log has no error and
no `box.audio` warning. It leaves out the main thread stall of `audio_tone.mjs`. `--safaridriver`
or `$SAFARIDRIVER` selects the driver, `/usr/bin/safaridriver` by default:

```sh
node test/integration/web/audio_tone_safari.mjs \
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
