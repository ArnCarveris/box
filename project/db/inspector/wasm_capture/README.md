# Capture on the web target

Implement box_capture_texture on the web target with asynchronous buffer mapping so capture and /capture work in wasm images.

## Details

The native capture copies the frame target into a buffer, maps it with a blocking poll loop and writes a PNG. The web target cannot block on the map, so the renderer keeps a small queue of pending captures: after the copy is submitted the buffer is mapped asynchronously and the callback, which Emscripten runs from the browser event loop, converts the rows and writes the PNG into the Emscripten file system at the requested path. The extract side already treats the file as ready only once it exists, so `/capture` returns 202 until the callback wrote it and 200 afterwards; the resource endpoint reads it from the same file system.

Verification: the renderer test for the capture path where possible, and a browser check of the wasm image from the inspector: `capture` prints a path whose thumbnail arrives, and the render pane refresh at 1 Hz returns PNGs in wasm mode. Record the check in the deliverable.

## References

- `src/renderer/capture.c`, `src/renderer/frame.c`
- `inspector/capture_endpoint`
