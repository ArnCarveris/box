# Inspector page

`apps/box_inspector/index.html` is one static file: an `inspector-core` script (transports, session,
paths, resources, history) plus a UI script and the status page tokens. `node apps/box_inspector/test.js`
runs that core out of the HTML against stubbed `fetch` and `boxRequest`: 36 cases, `tests.json`. The
browser runs used Chrome 153 headless over CDP, page served by `python3 -m http.server 8091` from the root.

`http.png`, against `box_app --headless --timeout infinite --fps 30 --port 27750` with a 640x360 `Surface`
and `etc/scenes/primitives.flecs`: session `s3`, 28 help lines, `ls`, `cat camera`, capture at 1 Hz with
the frame from `get box.core.App App` in the caption, a new image after `set camera Position "{2, 6, -16}"`,
`nope` highlighted from status 400, `capture` rendering an inline thumbnail linking to `/resource/...`, and
`clear` emptying the log with no request.

`wasm.png`, against a `--target em --cfg release` image of this checkout: `box_request` answers
`/terminal/new`, `help`, `ls` and `cat`, and `?scene=` renders live in the page canvas.

Not working: `/capture` in a wasm image always answers 202 and `/resource` 404, because
`box_capture_texture` (`src/renderer/capture.c`) is a no-op on web, so the capture toggle stays "capture
pending". `project/db/command/deliverable/wasm/` has no `box_request` yet, so a fresh image was used.
