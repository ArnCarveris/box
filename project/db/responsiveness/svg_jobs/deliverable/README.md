# Incremental SVG rasterization

Setting `Svg` no longer rasterizes inline: `src/texture/svg_job.c` builds one image per
request and submits one `box_job_submit` per horizontal band, with the position of the
material nearest the eye that binds the texture (`box_texture_refs_nearest`) or the
position of the `Svg` entity itself, so near textures come out first. A band is at most
256k texels, so a 1024 px icon is four jobs of about 3 ms; the band count comes from the
requested size, the first band that gets the image lock parses the SVG, the bands render
disjoint rows of one buffer and the last one defringes it and builds the mip chain. Only
the upload is left for the completion on the main thread, inside the `JobSettings` budget.
Until it lands the entity keeps `RenderTexture.handle` 0, or the image it already had while
resizing, and `TextureImpl.pending` says a job is out. A new set supersedes the queued
bands with `box_job_supersede`, deleting the entity cancels them, and a task is only freed
once a cancel no longer finds its job, which is the moment no worker can reach it.
`src/render/extract.c` calls `box_job_set_eye` with the eye of the first view per frame.

18 tests pass (11 `Svg`, 7 `SvgJobs`). `Svg.rasterize_256` costs 0.69 ms including the job
round trip, against 0.65 ms inline and 0.39 ms for nanosvg.
`SvgJobs.rasterize_1000_while_rendering` costs 0.76 ms per frame, worst 4.52 ms, while a
thousand icons rasterize. `apps/box_app/etc/scenes/svg_storm.flecs` runs 240 of them
headless at a worst frame of 0.23 ms (`svg_storm.json`); 240 is the ceiling because the
renderer allows 256 array layers per texture size class. `tools/integration.py` now gives
its visual harness `JobSettings: {workers: -1, budget_ms: 0}` so every rasterization lands
before the captured frame.
