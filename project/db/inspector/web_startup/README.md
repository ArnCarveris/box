# Web startup uploads dropped

A wasm image renders only the clear color until a component changes because packets processed before the asynchronous device is ready are discarded.

## Details

On the web the device and surface are created asynchronously while the game loop already produces packets; the renderer drains the queue after OnStore and, until the device exists, drops or ignores the packets, losing the mesh creation commands and the static instance deltas that only ever appear once. A later component change re-extracts that entity, which is why setting a component makes objects appear. The fix must keep every packet that carries resource commands or static deltas until the device is ready, or replay the resource state once it is, without changing the native path.

Verification: a reproducer that feeds packets to the renderer before the device is ready and checks that meshes and static instances exist after readiness, testable natively by delaying device creation in a test hook or by a queue level test, plus a browser check that a freshly loaded wasm image shows the primitives scene on its first frames without any command being sent.

## References

- `src/renderer/device.c`, `src/renderer/renderer.c`, `src/renderer/queue.c`
- `project/db/inspector/wasm_capture/deliverable/README.md` where the bug was observed
