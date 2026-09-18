# Inspector page

A static web page with a command console and a 1 Hz render view that connects to a running instance over HTTP or to a wasm image loaded in the page, linked from every epic on the status page.

## Details

The inspector is one HTML file, `apps/box_inspector/index.html`, with no build step. It has a connection bar that takes either an HTTP endpoint (default `http://localhost:27750`) or a wasm image URL, a console that creates a terminal session on connect and sends lines to `/terminal/exec`, showing the log with history, and a render pane that captures the active camera at a configurable rate (1 Hz by default, pausable) and displays the PNG. Both transports implement the same `request(method, path, body)` interface so the page code never knows which one it talks to.

The wasm transport uses the trick of the flecs explorer: the image exports `box_request` with `EMSCRIPTEN_KEEPALIVE`, the page loads `box_app.js` with its canvas and calls the export through `ccall`; the request is routed through the same endpoint handler as HTTP with `ecs_http_server_request`, so no server listens in the browser. Image replies come back base64 encoded from the export. A `/capture` endpoint triggers a capture and returns the latest PNG so the render pane needs one request per refresh.

The project tool serves the page at `/inspector` and every epic with `deliverable/wasm` links to `/inspector?wasm=<image url>`; a header link connects to a local native instance. Verification is browser driven against a native headless instance and the command epic wasm image, with screenshots in the deliverable, plus unit tests for the endpoint and the export.

## References

- `design/DESIGN.md`, section "Inspector"
- flecs `src/addons/app.c`, `flecs_explorer_request`
- `apps/box_app/shell.html` for the current wasm shell page
