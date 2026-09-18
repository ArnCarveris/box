# Latest capture endpoint

A /capture endpoint that triggers a capture of the active or named camera and returns the most recent PNG, usable over HTTP and through the wasm request export.

## Details

`GET /capture?session=<name>[&camera=<path>]` sets `Capture` on the active or named camera with a fixed path `<session>/resources/live.png`, then replies with the most recent completed capture from that path as `image/png` (status 200), or 202 with an empty body when no capture has completed yet. Because a capture completes on a later frame the endpoint always returns the previous frame's image and queues the next one, which is exactly what a 1 Hz refresh needs; a `&wait=1` variant is not provided since handlers cannot block the frame.

The session's `Terminal` must exist; an unknown session is 404 and an unknown camera 400. The endpoint reuses the capture command's helpers so the file naming stays in one place. On wasm the renderer writes the PNG into the Emscripten file system and the wasm request export base64 encodes it.

Verification: unit tests in `test/command` using `ecs_http_server_request` for the 202 then 200 sequence with a headless surface (skipping when no renderer can be created, like the capture task's end to end test), the named camera variant and the error codes.

## References

- `command/capture` for the `Capture` component and helpers
- `command/http` for the endpoint handler
