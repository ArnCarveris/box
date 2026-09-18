# Incremental SVG rasterization

SVG rasterization moved onto the job system and prioritised by distance to the camera.

## Details

Setting Svg queues a rasterization job, the texture shows a placeholder until it completes and the upload happens on the main thread. Jobs are prioritised by the distance of the nearest user of the texture to the camera and cancelled when the entity is destroyed or the size changes again. Large images are rasterized in bands so a single job never runs long.

Verification: unit tests cover queueing, cancellation, the placeholder state, band splitting and completion ordering by distance. A benchmark rasterizes a thousand SVGs while rendering and reports the worst frame time.
