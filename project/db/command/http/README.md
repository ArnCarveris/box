# HTTP command endpoints

An HTTP server singleton with /terminal/new, /terminal/close, /terminal/exec and /resource endpoints, dequeued once per frame.

## Details

`HttpServer` is a singleton component with a `port` member; setting it starts an `ecs_http_server_t` (an OnSet hook, remove stops it) and a system in OnLoad calls `ecs_http_server_dequeue` so request handlers run on the main thread. `box_parse_args` gains `--port <n>` which sets the singleton; the default port is 27750 and 0 disables the server. The request callback dispatches on the path prefix and returns text/plain replies with a concise body and an HTTP status.

`/terminal/new` creates a child of `terminals` with a unique name and a `Terminal` component and replies with the name. `/terminal/close?session=<name>` deletes it. `/terminal/exec?session=<name>&cmd=<line>` (GET with a URL encoded cmd, or POST with the line as body) sets `TerminalInput` and replies with `TerminalOutput`, status 400 when the output has the error flag, 404 for an unknown session. `/resource/<path>` streams a file from the session resources folder of the session given by the `session` query parameter; any path containing `..` or resolving outside that folder is refused with 403, and the content type is image/png for `.png` and text/plain otherwise.

Tests in `test/command` use `ecs_http_server_request` to exercise every endpoint without sockets, including the traversal refusal and the error status. The world is progressed between requests where the design needs it. Keep the handler file under 400 lines; encoding helpers go in their own file.

## References

- flecs `include/flecs/addons/http.h`
- `command/terminal` for the session components
