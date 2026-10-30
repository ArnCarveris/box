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
delta of 1/60 s. At frame 30 the scene moves a square and changes a color, a fade and an
emission, with transitions of 3 s. The check takes screenshots until the square and the color
reach their targets and passes when one screenshot shows the square in between and one the
color, the square never moves back, both end at their targets and the log has no error and no
invalid WebGPU object. It takes the same arguments:

```sh
node test/integration/web/transitions.mjs \
    "$(tools/integration.py local-env --env <name>)/wasm32-Emscripten/debug/bin"
```
