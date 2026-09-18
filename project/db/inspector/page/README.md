# Inspector page

The single file inspector page: connection bar for an HTTP endpoint or a wasm image, a command console with history and log, and a render pane refreshing at a configurable rate.

## Details

`apps/box_inspector/index.html` is a static page styled like the project status page (same tokens, light and dark). The connection bar has a mode toggle: HTTP with an endpoint field, or wasm with an image URL; connecting creates a session with `/terminal/new`, closes it on disconnect or page unload, and shows the session name. The console has an input with history (up and down arrows), sends each line to `/terminal/exec` and appends `$ line` and the result to a scrolling log; errors are highlighted. A render pane requests `/capture` at the chosen interval (1 Hz default, pause button) and shows the PNG; the pane also shows the frame number from `get App` when available.

Transports implement `request(method, path, body)` returning `{status, type, body}`: HTTP uses `fetch` with CORS friendly requests (verify the flecs HTTP server sends the CORS headers, add them in the endpoint handler if not); wasm loads `box_app.js` into the page with a `<canvas>` (the shell's Module setup), waits for the runtime, and calls `ccall('box_request', 'string', ['string', 'string', 'string'])`, decoding the reply format from the request export task. The wasm image URL is passed as `?wasm=<url>` and the HTTP endpoint as `?http=<url>` so links can preconnect.

Verification: the page's transport and reply parsing code is factored so a node script in `apps/box_inspector/test.js` can run it with a stubbed request; a manual run against a native headless instance and the command epic wasm image is recorded with screenshots in the verification task.

## References

- `design/DESIGN.md`, section "Inspector"
- `project/index.html` for the shared look
