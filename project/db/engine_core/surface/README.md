# Surface and renderer thread

Setting Surface brings up a window or offscreen target; a renderer thread clears and presents frames from a packet queue.

## Details

`box.surface` owns the reflected `Surface` component. Its on_set hook creates a GLFW window (or nothing when headless) and a renderer, later sets apply size and title. `BoxSurfacePoll` runs in OnLoad to pump events and publish the actual and point sizes, `BoxSurfaceSubmit` runs in OnStore and fills a packet with frame number, time, size, clear color and an optional capture path.

`src/renderer` never includes flecs. It owns a three packet queue with free and ready lists, a thread that initializes the WebGPU device and renders packets, offscreen color and depth targets when there is no surface, a placeholder triangle pipeline, and texture readback to PNG through stb_image_write. On web the renderer runs on the main thread and device creation completes through spontaneous callbacks. `src/platform` wraps GLFW plus a CAMetalLayer on macOS and the HTML canvas on web.
