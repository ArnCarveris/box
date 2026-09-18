# WebAssembly build

Emscripten target with a shell page, main-thread renderer and a runnable html artefact.

## Details

`bake3 build box_app --target em` compiles the package with the emdawnwebgpu port and links the app with a shell page that hosts the canvas, forwards URL hash arguments to `main` and hides its overlay on the `box-first-frame` event the renderer raises. The renderer requests adapter and device with spontaneous callbacks and drains the packet queue on the main thread; presenting is implicit on web.
