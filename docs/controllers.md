# Controllers

Input-driven movers for cameras and first-person bodies. Module `box.controllers`
(`BoxControllersImport`, imports `box.types`, `box.transform`, `box.input`). A controller reads
the `ActionState` of named actions in its action scope, never a device: remap by editing the
`Binding` of those actions or by pointing the controller at your own scope.

## Components

| Component | Key fields (defaults) | What it does |
|---|---|---|
| `CameraController` | `speed` 40, `boost` 5, `acceleration` 6, `deceleration` 18, `turn_speed` 1.5, `turn_acceleration` 5, `turn_deceleration` 15, `boost_yaw` 0.33, `sensitivity` 0.003, `zoom` 3, `invert_y`, `actions` (`box.controllers.camera`), `collide` false, `radius` 0.25 | Free-fly camera. Writes `velocity`, `angular_velocity` |
| `FirstPersonController` | `speed` 4, `crouch_speed` 2, `fly_speed` 10, `boost` 4, `jump` 5, `gravity` 18, `eye` 1.7, `crouch_eye` 0.9, `ground` 0, `radius` 0.3, `step` 0.3, `run` 0 (Fast ignored), `climb` 2, `swim_speed` 0.45, `swim_jump` 3.5, `swim_depth` 1.3, `swim_step` 3.2, `sensitivity` 0.003, `acceleration` 0 (snap), `turn_speed` 1.5, `turn_acceleration` 5, `turn_deceleration` 15, `invert_y`, `actions` (`box.controllers.first_person`), `flying` | Walking body with gravity, jump, crouch, fly toggle. Writes `velocity`, `angular_velocity`, `height`, `grounded`, `crouched`, `climbing`, `swimming` |
| `Climbable` | tag, on a shape or an ancestor, inherited through IsA | A first person controller touching it climbs it like a ladder: Forward up, Back down at `climb` speed, Forward steps off at the top |
| `TriangleCollider` | tag, on a mesh shape | Controllers collide with the triangles of the mesh instead of its bounding box |
| `Vessel` | `route` (closed `Path3D`), `pivot`, `dock` 0, `speed` 6, `accel` 0.3, `wait` 25, `max_speed` 9, `max_yaw_rate` 0.12, `yaw_response` 2.5, `steer_heel` 0.6, `max_heel` 0.105, `waves` (pitch, roll, heave), `outline` (`Path2D`), `keel` -5, `deck` 5, `inset` 1.2, `water` 0, `stand`, `reach` 1.4, `wheel`, `lever`, `throttle_rate` 0.35, `rudder_rate` 0.9, `rudder_return` 0.5, `actions` (`box.controllers.helm`) | Ship that cruises its route and docks, or that the first person controller that used `Use` at the stand steers (throttle, rudder, hull kept off ground and quays). Writes `Position`, `Rotation`, wheel `Rotation.z`, lever `Rotation.x` |
| `VesselStatus` | `mode` (`VesselDocked`, `VesselCruising`, `VesselManual`, `VesselRejoining`), `distance`, `speed`, `heading`, `yaw_rate`, `throttle`, `rudder`, `wait`, `to_dock`, `heel`, `pitch`, `roll`, `heave`, `x`, `z`, `pilot`, `bumps`, ... | State of a `Vessel`, written every frame; set `mode` or `distance` to seek |

Both imply `Position` and `Rotation` and write them (local space) with `ecs_modified`.
Defaults come from the component constructor, so `CameraController: {speed: 12}` keeps the rest.

## Default actions

| Action (under scope) | Camera key | First person key |
|---|---|---|
| `Forward` / `Back` / `Left` / `Right` | W / S / A / D | W / S / A / D |
| `Up` / `Down` | E / Q | E / Q (free flight only) |
| `Fast` | ShiftLeft (+ child binding `ShiftRight`) | same (flight boost) |
| `Look` | MouseRight (hold, cursor turns) | MouseRight |
| `TurnLeft` / `TurnRight` / `TurnUp` / `TurnDown` | arrow keys | arrow keys |
| `Jump` | - | Space |
| `Crouch` | - | CtrlLeft |
| `Fly` | - | F (toggles `flying`) |

Scopes: `box.controllers.camera`, `box.controllers.first_person`, `box.controllers.helm` (`Use` F, `Ahead` W, `Astern` S, `Port` A, `Starboard` D, `Stop` X, `Centre` Space, `Look`, turn actions). The actions of a scope are
disabled (do not bind, are not listed) while no controller uses that scope.

## Flecs script

```flecs
using box.transform
using box.camera
using box.input
using box.controllers

camera {
  Position: {0, 2.2, 7}
  Rotation: {-0.2, 0, 0}
  Camera: {fov: 60}
  CameraController: {speed: 12}
}

// Collide with the shapes of the camera's scene instead of flying through them
probe { Camera: {}; CameraController: {collide: true, radius: 0.4} }

player {
  Position: {0, 1.7, 5}           // the eye, not the feet
  Camera: {fov: 70}
  FirstPersonController: {}
}

// Own action scope: the controller looks up children by the action names above
my_keys {
  Forward { Action: {}; Binding: {key: Up} }
  Back { Action: {}; Binding: {key: Down} }
  Look { Action: {}; Binding: {key: MouseLeft} }
}
rig { Camera: {}; CameraController: {actions: my_keys} }
```

Missing actions in a custom scope are simply never held.

## Systems

| System | Phase |
|---|---|
| `CameraActionsActivate`, `FirstPersonActionsActivate` | `EcsPreFrame` (enable/disable the default scopes) |
| `HelmActionsActivate` | `EcsPreFrame` |
| `VesselUpdate` (before the controllers, so riders and the pilot follow in the same frame) | `EcsOnUpdate` |
| `CameraControllerUpdate`, `FirstPersonControllerUpdate` | `EcsOnUpdate` |

## Behavior and gotchas

- Every enabled entity with a controller moves: several cameras with `CameraController` on the
  same scope all fly together. Disable the entity or remove the component on inactive cameras,
  or give each its own `actions` scope.
- Camera: Forward/Back/Left/Right move along the yaw in XZ, Up/Down along Y, the wheel pushes
  along the view (`zoom`). Velocity and turn ease at `e^-(rate*t)`; a rate of 0 never changes.
- Yaw is `Rotation.y`, kept in [-pi, pi] (a value set outside is rewritten next frame with an
  OnSet). Pitch is `Rotation.x`, clamped 0.05 rad short of vertical.
- `Look` adds the cursor delta of its `ActionState` times `sensitivity` while held and stops on
  release. The wheel reads `Input.mouse.scroll`, which excludes the wheel over an `InputBlocker`.
- First person: `Position` is the eye. The body is a capsule from the feet to just above the eye
  that slides along shapes, stands on them, steps onto ledges up to `step` and only stands up
  when there is room. `ground` is a plane that holds the feet only where no shape is below.
- Collision uses the spatial index of the entity's render scene (`box_spatial_overlap_box`),
  with conservative advancement in steps of half the radius; the controlled entity's own shapes
  are ignored. A camera only collides with `collide: true`.
- The frame `dt` the controllers use is clamped to 0.5 s.

## Tests

```sh
bake3 run test/cpu/controller --local-env -- Camera   # also Collision, FirstPerson
```
