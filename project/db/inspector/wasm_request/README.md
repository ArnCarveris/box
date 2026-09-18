# Request export for wasm images

An exported box_request function on the wasm target that routes a method, path and body through the command endpoints without sockets, mirroring flecs_explorer_request.

## Details

On the wasm target `HttpServer` cannot listen, so the http task's server is created but not started (verify what `ecs_http_server_init` and `ecs_http_server_start` do under Emscripten and keep the handler reachable). `box_request(method, path, body)` is an `EMSCRIPTEN_KEEPALIVE` function in `src/command/wasm.c` compiled only for `BOX_PLATFORM_WEB` that calls `ecs_http_server_request` on that server and returns a static reply string of the form `<status>\n<content type>\n<body>`; a body with content type `image/png` is base64 encoded. The same function is compiled natively under a different name (`box_request_native`) so it can be unit tested without a browser.

The wasm shell `apps/box_app/shell.html` exposes the export through `Module.ccall` and stays usable standalone. The app main on wasm sets `App.port` so the server object exists, and the emscripten build flags export the function (check `-sEXPORTED_FUNCTIONS` and `-sEXPORTED_RUNTIME_METHODS` in `project.json`).

Verification: unit tests in `test/command` for the native twin covering a terminal round trip through the reply format, a 404 path and the base64 image reply; a manual check that `bake3 run box_app --target em` serves a page from which `Module.ccall('box_request', ...)` returns the `help` output, recorded as a short note in the deliverable.

## References

- flecs `src/addons/app.c`, `flecs_explorer_request`
- `src/command/http.c`, `src/command/endpoint.c`
