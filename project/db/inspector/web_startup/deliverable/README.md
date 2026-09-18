# Web startup uploads

Root cause: `box_renderer_consume` released a packet without applying it while
`r->ready` was false, and `box_renderer_submit` drained the queue on every submit. On
web the device is requested asynchronously, so the first packets were dropped with the
`mesh_create` commands and `static_updates`/`static_counts` deltas, emitted once only.

Fix (`src/renderer/renderer.c`): drain only while the device is ready, from
`box_renderer_submit` and now also from `box_renderer_acquire`. Held packets stay queued
and are applied oldest first once the device exists. With the pool of three empty the
game side gets no packet, skips extraction and keeps its pending meshes and dirty
entities, so nothing is lost. Native is unchanged; a packet released unapplied now frees
the arrays it owns.

Browser check, Chrome 153 headless (SwiftShader), scene `primitives.flecs` in the URL
hash and `capture` the only command sent: before, `capture_166_0.png` is 738560 pixels
of one color, the clear color; after, `capture_169_0.png` (`first_frames.png`) has 2705
colors with 49.5% of pixels differing from the corner pixel, and the canvas screenshot
agrees. Tests: core 27 (2 new `Queue` cases), render 37, command 150 pass, lint clean.
