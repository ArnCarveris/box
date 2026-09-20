# Command port drops requests in headless runs

## Built
`BoxHttpDequeue` (`src/command/http.c`) hands `ecs_http_server_dequeue` the wall time since
the previous dequeue, kept as `HttpEndpoint.clock`, instead of the frame delta. A headless
loop fixes the delta at 1/60 s and runs thousands of frames per second, so the server's one
second connection purge fired about 12 ms after a socket connected.

## Verified
`test/command` Http: `a_connection_survives_a_burst_of_fast_frames` connects a socket on its
own port, progresses 1200 fast frames, then sends a request and reads `HTTP/1.1 200`; it failed
on `got > 0` before the fix. Suite green under `-j 8`. Against a headless `box_app` on the
default scene, 30 of 30 `/terminal/new` requests answered (13 of 30 before).
No per frame work changed; `bench: false`.
