# Camera

`box.camera` (`BoxCamera`, `include/box/camera.h`) registers the `Camera` component and builds its
reversed Z projection. A camera renders only when a `View` points at it (`docs/view.md`). It looks
down its local -Z axis; it renders the scene it belongs to (nearest ancestor with `Scene`).

## Camera

| Field | Default (ctor) | Notes |
| --- | --- | --- |
| `fov` | 60 | Vertical field of view in degrees (perspective) |
| `near_` | 0.1 | Near plane. Perspective with `near_ <= 0` draws nothing |
| `far_` | 1000 | Far plane. Perspective `0` = infinite far plane. `far_ <= near_` (other than perspective 0) draws nothing |
| `aspect_ratio` | 0 | Width / height; 0 follows the surface |
| `ortho` | false | Orthographic projection |
| `ortho_height` | 10 | Visible height in world units when ortho |
| `ortho_scale` | 0 | Surface points per world unit; overrides `ortho_height` when > 0 |

Note the trailing underscores: `near_`, `far_`. Values apply as given; a C literal `(Camera){...}`
skips the ctor defaults, so set every field you need.

```flecs
using box.transform
using box.camera
using box.controllers

camera {
  Position: {0, 2.5, 6}
  Rotation: {-0.35, 0, 0}          // pitch down; yaw in y
  Camera: {fov: 42, near_: 0.5, far_: 1400}
  CameraController: {speed: 55}    // fly controls, from box.controllers
}

hud {
  Scene: {}
  camera {
    Position: {0, 0, 100}          // in front of the z = 0 UI plane
    Camera: {ortho: true, ortho_scale: 1}   // 1 world unit = 1 surface point: screen space UI
  }
}
```

## Rules and gotchas

- Give the camera a pose (`Position`, `Rotation`) so it has a `WorldTransform` that places it. `Rotation` is yaw/pitch/roll as in `docs/transform.md` (pitch + yaw keeps the horizon level).
- Depth is reversed: 1 at the near plane, 0 at the far plane (or infinity for perspective `far_: 0`).
- A camera without a depth range warns on set (`camera '<path>' has near .. and far .., which leave no
  depth: it draws nothing`).
- Ortho UI: `ortho_scale: 1` makes positions and sizes surface points with the origin at the center,
  y up. Projection anchors (`docs/projection.md`) assume this.
- Shadows, culling and LOD depend on the view's camera; `ShadowSettings.interval` re-renders all
  cascades when the camera projection changes.

## C API

| Function / macro | Does |
| --- | --- |
| `box_camera_projection(&cam, width, height, out)` | Reversed Z projection for a surface size in points; aspect from the surface unless `aspect_ratio` set; `ortho_scale` divides the surface height into the visible height |
| `box_camera_has_depth(&cam)` | Ortho: `far_ > near_`; perspective: `near_ > 0` and `far_ == 0` or `far_ > near_` |
| `BOX_CAMERA_FOV` / `_NEAR` / `_FAR` / `_ORTHO_HEIGHT` | Ctor defaults 60, 0.1, 1000, 10 |

## Shell

- `fit [camera]`: move the active camera, or a camera, so it sees the whole scene.
- `capture [camera]`: PNG of the active camera or a camera as its view shows it (debug drawing
  included when a view renders it).
- `--camera <name>`: command line option, the camera the surface shows.
