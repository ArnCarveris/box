# FPS overlay

An FpsOverlay component that draws the frame rate and a bar chart of the last second of frame times on the view, green above 60, yellow above 30, red below.

## Details

`FpsOverlay {enabled, corner, scale, opacity}` on a view or as a singleton draws, after the effect chain and before capture, a small panel in a corner of the view: the frame rate as digits rendered from rectangles (a seven segment style needs no font, which the UI epic brings later) and one bar per frame of the last second, height proportional to the frame time with a marked 60 and 30 fps line, colored green when the frame stayed above 60 fps, yellow above 30 and red below. The frame times come from the packets, so the overlay reflects the real frame pacing including the renderer thread; the panel is drawn by a dedicated overlay pass from a small per view buffer, costs nothing when the component is absent, and is included in captures so a headless run can verify it.

Verification: unit tests for the ring of frame times, the color classification thresholds and the digit segment table; a visual scene with the overlay enabled under the fixed headless step, baselined (the fixed step makes the bars deterministic); the overlay's own cost measured with a headless run.

## References

- `src/renderer/frame.c`, the effect chain and capture order
- `design/DESIGN.md`, section "Agent based development" (builtin performance monitoring)
