# Inspector: completion report

## What was built
- `apps/box_inspector/index.html`: a single static page with a connection bar (HTTP endpoint or wasm image URL, preconnect through `?http=` and `?wasm=`, `&scene=` for the image arguments), a console with history, `clear` handled on the page, thumbnails for result lines that name a resource, and a render pane refreshing through `/capture` at a configurable rate with pause. Both transports implement one `request(method, path, body)` interface; `apps/box_inspector/test.js` covers the transports, reply parsing and console logic.
- `box_request(method, path, body)` exported from wasm images and routed through the command endpoints with `ecs_http_server_request`; the reply is `<status>\n<content type>\n<body>` with image bodies base64 encoded. The flecs HTTP server is created but never started on the web, which also removed a startup abort in every earlier wasm image.
- `GET /capture?session=[&camera=]` queues a capture of the active or named camera to `resources/live.png` and returns 202 until the renderer wrote it, then the PNG; CORS headers on the direct request path and an OPTIONS preflight.
- Capture on the web target with asynchronous buffer mapping into the Emscripten file system, so `capture` and the render pane work in wasm images.
- The status page serves the inspector at `/inspector`, every epic with a wasm deliverable links to it, and the header links to a local instance; deliverable files are served with wasm and javascript content types.
- Bug fixed along the way: packets produced before the asynchronous web device existed were released unapplied, so every wasm image rendered only the clear color until a component changed. Packets now stay queued until the device is ready.

## Verification
- Unit tests: 292 unit tests across test/core (27), test/transform (46), test/geometry (32), test/render (37) and test/command (150), all passing, see `tests.json` and `tests_<project>.json`; 36 page tests in `apps/box_inspector/test.js` (`verification/deliverable/tests.json`).
- Browser: `verification/deliverable/native.png` shows the page against a headless native instance with all fifteen scripted checks passing; `wasm.png` shows the page driving the epic wasm image with the scene rendering live and `/capture` cycling 202 then 200 every second; `wasm_old.png` shows the message for an image without the export.
- Integration: the three command scenes, the visual scene and the two stress benchmarks in `integration/` all pass.
- Microbenchmarks in `bench/` compared against the command epic (`Dispatch.help` 8% slower because three commands were added, `Dispatch.ls_scope` 83% faster because `ls` no longer lists components, `Extract.dynamic_10k` 6% slower which is within the load noise measured in the command epic).
- Wasm image under `wasm`, open it through the status page Inspect link of this epic.

## Lessons
- flecs sets a thread hook on Emscripten, so `ecs_http_server_start` tries `pthread_create` and aborts in a non-pthread build; create the server object and never start it there.
- Headless Chrome on macOS has no WebGPU, so wasm checks need a headed browser; HTTP mode verifies headless.
- emdawnwebgpu completes spontaneous callbacks from the JavaScript promise, so no per frame event processing is needed for asynchronous mapping.
- The flecs HTTP server answers socket preflights itself without custom headers, so cross origin POSTs from the page must stay `text/plain`.

## Not in this epic
- The images of the engine core and scene core epics predate the export; their Inspect links render the scene only and say so. The command epic image was rebuilt from this epic's code because the original aborted at startup.
- The in-game console, the `render` command and the input commands remain with the UI, views and input epics.
