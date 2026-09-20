# Command port drops requests in headless runs

A headless loop passes its fixed frame delta to the HTTP server, so at thousands of frames per second the server purges a connection milliseconds after it opens and about half of the requests get an empty reply.

## Details

`BoxHttpDequeue` in `src/command/http.c` hands `it->delta_time` to `ecs_http_server_dequeue`.
A headless run progresses the world with a fixed delta of one sixtieth of a second per frame
whatever the frame took, and an uncapped headless loop runs several thousand frames per
second, so the server's clock runs about eighty times faster than the wall: its one second
connection purge fires about twelve milliseconds after a socket connects, before the client
has sent its request, and curl reports an empty reply. Measure the elapsed wall time between
two dequeues on the endpoint instead and pass that, so the purge and the request cache keep
their real timeouts under any frame rate.

Verification: a `test/command` Http case that connects a socket, progresses twelve hundred
fast frames, then sends a request and reads a 200 reply; the existing Http cases green; thirty
consecutive `/terminal/new` requests against a headless `box_app` all answered.

## References

- `src/command/http.c`, flecs `src/addons/http/http.c` (`ecs_http_server_dequeue`),
  `docs/ARCHITECTURE.md` (Command interface)
