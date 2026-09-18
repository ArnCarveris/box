# HTML5 input events

Map canvas keyboard, pointer, wheel and focus callbacks onto the same Input singleton on wasm.

## Details

The wasm platform layer registers canvas callbacks, translates browser key codes to the engine codes and queues the same events as the native path, including pointer lock for captured cursors. Focus and visibility changes follow the same clearing rules as native. The wasm build of the epic is the verification, together with the shared unit tests for the mapping table.
