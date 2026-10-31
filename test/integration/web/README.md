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
