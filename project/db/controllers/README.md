# Controllers

The default camera controller and a first person controller, both driven by named actions rather than raw keys.

## Details

Controllers sit on top of the action layer: the default camera controller with WASD, QE and mouse look, and a first person controller with jump, crouch and free flight. Both read named actions, so a project rebinds or scripts them by editing the InputMap instead of patching the controller. Collision against the scene is not part of this epic; the collision epic sweeps these controllers against its spatial index so no controller walks through the scene.

Verification is unit tests driven by injected events that move, turn and toggle a controller, command based integration tests that drive it from a terminal session, and visual walk throughs of a scene. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: CameraController and FirstPersonController.

## References

- design/DESIGN.md, section Controllers
- design/datamodel/camera.flecs (CameraController)
