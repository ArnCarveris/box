# Input and controllers

The device and action input layers with script and command access, and camera and first person controllers that collide with the scene.

## Details

The device layer is an Input singleton with per frame state for 128 keys, the mouse buttons as codes 128 and 129 and a text buffer, filled by one OnLoad system from OS events and injected events so that injected input is indistinguishable from real input. The action layer turns key masks with modifier bits into named action entities through an InputMap that is authorable from script, and actions integrate with the script event framework so a script can await a press. The same actions can be emitted by UI widgets or by a command, which is what lets a button and a key binding share one action.

Controllers sit on top: the default camera controller with WASD, QE and mouse look, a first person controller with jump, crouch and free flight, and collision against the interaction spatial index so no controller walks the scene. Verification is unit tests driven by injected events, command based integration tests that type, click and drag, visual walk throughs and the brief's benchmark of navigating a scene with a million objects at 60 FPS. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: Input, InputAction, InputMap and the keys constants, CameraController and FirstPersonController, plus the device and action command functions.

## References

- design/DESIGN.md, sections Input, Controllers and Command interface (input commands)
- design/datamodel/camera.flecs (CameraController)
