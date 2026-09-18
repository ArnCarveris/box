# Web capture

`box_capture_texture` on web submits the same texture-to-buffer copy as native, then maps the buffer
with `wgpuBufferMapAsync` in `WGPUCallbackMode_AllowSpontaneous`, so the browser event loop runs the
callback: it swaps BGRA rows, `mkdir`s the parent folders, writes the PNG with stb into MEMFS,
unmaps and releases the buffer. The renderer holds two slots (`BOX_CAPTURE_PENDING_MAX`); a frame
whose slots are all in flight skips the capture and the endpoint asks again a second later. Nothing
polls, so the web frame path is unchanged.

Checked against `bake3 build/run box_app --target em --cfg release` on `http://localhost:8080`,
driven over CDP from node, in Chrome 153 headless (SwiftShader) and headed (Metal-3 adapter):

- `capture` prints `capture_<frame>_0.png` and `/resource/<path>?session=s1` answers `200
  image/png` with a body decoding to `89 50 4e 47`, 124676 bytes of `primitives.flecs` (`capture.png`)
- `/capture?session=s1` answers 202 once and `200 image/png` on every call after, five times at 1 Hz
- native untouched: `test/render` 37, `test/command` 150, `test/core` 25 pass, lint 0 problems

A wasm image renders the clear color until a component is set; the live canvas is empty the same way
before any capture, so the startup upload, not the capture path, drops those first frames.
