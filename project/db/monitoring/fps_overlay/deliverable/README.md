# FPS overlay

`FpsOverlay {enabled, corner, scale, opacity, fps, mean_ms, worst_ms}` on a view
or as a singleton draws a panel in a corner of the view: the frame rate as seven
segment digits and one bar per frame of the last second, green above 60 fps,
yellow above 30 and red below, with both rates marked as lines. The renderer
keeps the ring from the interval between the packets it processes, builds the
rectangles of the panel and draws them from a per view buffer in a dedicated
pass after the effect chain and before the capture, so the panel is in
screenshots and in the inspector. `src/monitor/fps.c` publishes the same window
on the component for the terminal and the REST API.

Verified: 20 new cases in `monitor.Fps` (ring, window, thresholds, colors,
segment table, panel layout and the published window), 53 in `test/monitor`,
lint 0, `fps_overlay.png` baselined and bit identical over three runs, all 44
visual scenes unchanged. A headless run of 3000 frames with and without the
panel takes 4.89 s either way, so its own cost is under 0.01 ms per frame.

Left out: no font and no GPU timings in the panel, both of later epics.
