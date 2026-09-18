# Auto exposure and tone mapping

Histogram based auto exposure and the tony tone mapper with dithering as the final stage of the graph.

## Details

A compute pass builds a luminance histogram of the color target, takes a weighted average and adapts the exposure over time with configurable speed, minimum and maximum. The tone map stage applies the tony curve and then ordered dithering before writing the target, so gradients do not band on eight bit outputs. AutoExposure and ToneMapping are components, so a scene can pin the exposure or change the curve at runtime.

Verification: unit tests check histogram binning and the adaptation curve on known inputs and cover a pinned exposure. Visual scenes move a camera from a dark room into sunlight and capture the adaptation over frames, and a gradient scene shows that dithering removes banding. This also resolves the color clipping recorded in the scene core report.
