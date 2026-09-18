# Capture command

capture renders the active or named cameras to PNG files in the session resources folder through a Capture component.

## Details

`box.camera` gains a `Capture` component with a `path` string; when set on a camera entity, the extract system uses that camera as the packet view for the next frame, writes the path into the packet capture path and removes the component. The renderer already writes the packet capture path after rendering, so the file appears once the renderer has processed that frame. When multiple cameras have `Capture`, one is honored per frame in entity order.

`capture` (no arguments) sets `Capture` on the active camera (the camera entity used by the extract system) with a path `<session>/resources/capture_<frame>.png` and prints the path relative to the session folder. `capture cam` and `capture cam0,cam1,...` do the same for each named camera, printing one path per line. Agents fetch the files through `/resource/<path>`, which returns 404 until the renderer wrote the file; the terminal task's output states that.

Tests in `test/command` and `test/render` cover the component being consumed by extract, the path in the packet, the multi camera case, and an end to end headless run in a test that progresses the world and waits for the file. Add a `test/integration/visual` scene using a `Capture` component to prove the data driven path.

## References

- `command/http` for the resources folder and `/resource`
- `src/render/extract.c` and `src/surface/surface.c` for the capture path plumbing
