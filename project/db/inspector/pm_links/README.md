# Status page links

The project tool serves the inspector page and links every epic with a wasm deliverable to it, plus a link for a locally running instance.

## Details

`project/pm.py` serves `apps/box_inspector/index.html` at `/inspector` and the wasm images at their existing `/deliverable/<epic>/deliverable/wasm/` URLs with the right content types (`application/wasm`, `application/javascript`). The epic cards and the epic detail panel get an "Inspect" link to `/inspector?wasm=/deliverable/<epic>/deliverable/wasm/box_app.js` when the epic has a wasm deliverable, and the page header gets an "Inspect local instance" link to `/inspector?http=http://localhost:27750`.

The wasm shell page fetches its scene files relative to its own URL; make sure the deliverable folder contains everything the image needs (check `apps/box_app/shell.html` and the emscripten preload settings) or that the inspector passes a scene through the URL hash the shell already supports. Tests in `project/test_pm.py` cover the inspector route, the content types and the link generation in the state JSON.

## References

- `project/pm.py`, `project/index.html`
- `README.md`, section "WebAssembly"
