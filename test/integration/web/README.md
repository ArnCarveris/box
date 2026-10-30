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

Before starting Chrome on macOS, both scripts check the `.app` bundle that contains the Chrome
executable (after resolving symlinks) for the `com.apple.quarantine` attribute
(`xattr -p com.apple.quarantine <app>`).
macOS holds a quarantined app, such as one installed by Homebrew, at its first launch check, and
no one can confirm the dialog of a headless session, so the scripts fail at once with exit status
2 and name the app instead of reporting "chrome did not start" after the connect timeout. Open
Chrome once from the Finder, run `xattr -dr com.apple.quarantine <app>`, or pass `--chrome` with a
copy without the attribute. The check is skipped off macOS, without `xattr`, and for a Chrome
outside a `.app` bundle. `quarantine.sh` checks this with a fake quarantined `Foo.app`:

```sh
test/integration/web/quarantine.sh
```

When Chrome still hangs at the start ("chrome did not start", while `Google Chrome --version`
does not return either), look for another launch check holding it the same way.
