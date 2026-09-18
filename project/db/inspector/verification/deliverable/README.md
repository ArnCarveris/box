# Inspector verification

Browser driven over CDP from node, page served by `python3 project/pm.py serve --port 8093`,
Chrome 153. `tests.json` is `node apps/box_inspector/test.js --json`: 36 pass. No bug was found in
the page, so `index.html` and `test.js` are unchanged.

`native.png` (1280x800, headless Chrome), against `box_app --headless --timeout infinite --fps 30
--port 27750` with a 640x360 `Surface` and `etc/scenes/primitives.flecs`, opened as
`/inspector?http=http://localhost:27750`. All 15 checks pass: connect and the automatic `help`,
`pwd`, `ls`, `cd box` then `pwd`, `cat camera`, `set camera Position` giving a different render
within a second, the pane refreshing every second with the frame number from `get box.core.App
App`, `capture` showing a thumbnail that links to `/resource/<file>?session=<id>` answering `200
image/png`, a 1600 character line going out as a `POST` with `Content-Type: text/plain`, `clear`
emptying the log without a request, and **Disconnect**. No console errors.

`wasm.png`, against the epic image in `project/db/inspector/deliverable/wasm/` (built from 379fc53)
opened as `/inspector?wasm=...&scene=etc/scenes/primitives.flecs`. The console answers `help`,
`ls`, `cat camera` and `set camera Position` through `box_request`, the canvas renders the scene,
`/capture` answers 202 once and then `200 image/png` every second, and `capture` shows a thumbnail
from a `data:` URL, because a wasm image has no URL to link to.

Not working, and not page bugs:

- Headless Chrome has no `navigator.gpu` on macOS, so the wasm run needs a headed browser.
- `wasm_old.png`: the `scene_core` image predates the export and the console says so, as designed,
  but its canvas stays at the clear color, the `inspector/web_startup` bug that made every image
  built before 379fc53, including the first one built here, render nothing.
- The `command` epic image fails to start at all: `Aborted()` from emscripten, in the inspector
  and in its own `box_app.html`, so that epic's **Inspect** link only shows the error.
