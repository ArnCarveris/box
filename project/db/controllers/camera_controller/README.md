# Camera controller

The default camera controller: WASD in the XZ plane, QE for height and the mouse for direction.

## Details

CameraController on a camera entity enables fly movement with configurable speed, acceleration and mouse sensitivity, and the terrain entity and clearance from the datamodel keep the camera above ground when set. It binds to actions rather than raw keys so everything can be rebound, and the engine ships a default map. Mouse look only rotates while the cursor is captured, and a modifier moves faster.

Verification: unit tests drive injected input and check the resulting position and orientation over frames, including clearance, sensitivity and the speed modifier. A command based integration test flies the camera through a scene and captures frames along the way.
