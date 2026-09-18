# GLFW input events

Map GLFW key, character, mouse, scroll, focus and iconify callbacks onto the Input singleton.

## Details

The platform layer translates GLFW key codes to the engine codes, queues events instead of writing the singleton directly so the OnLoad system stays the single writer, and reports focus and minimise changes. Cursor capture is driven by the Surface cursor_captured field. Unit tests cover the code mapping table and the queue, since the callbacks themselves need a window.
