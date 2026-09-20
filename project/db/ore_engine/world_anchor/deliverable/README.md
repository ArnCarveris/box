# World anchored UI elements

## Built
`WorldAnchor {entity, offset, view}` in `box.ui` (`src/ui/world.c`). The layout places the
element where the camera of the view (the active camera when `view` is 0) projects the world
position of the entity plus the offset, in the logical pixels of its tree; it takes no room in
the row or column of its parent and the Layout of its own children keeps working. A point
behind the camera or outside the view gives the element `Hidden`, which the anchor removes
again itself; a `Hidden` an author set is left alone (a private `UiWorldHidden` tag remembers
whose it is). A PostUpdate pass after the transforms re-projects every anchor, marks the tree
of one that moved and lays it out in the same frame, so a bar over a moving crate never lags;
a still scene marks nothing. Cost: one projection per anchored element per frame.

## Verified
`test/ui` WorldAnchor, 11 cases: orthographic and perspective projection, following an entity,
the offset, hidden behind the camera and outside the view with the element shown again,
an authored Hidden kept, no room in the flow, children laid out under the anchor, a target
moved by Velocity landing in the same frame, a still scene laying out nothing. Suite green.
`ui_world_anchor.flecs` (screenshot here, baseline created): bars over three crates, a labelled
bar with a Column layout, a crate moving 2 units per second with its bar on it in the captured
frame, and a crate behind the camera whose bar draws nothing.
The layout pass is an immediate system in PostUpdate, which puts a merge before extract that
the frame did not have: `sky_fog`, `time_of_day_*` and `weather_*` (eight scenes) then render
the sun rotation and sky state of their own frame instead of the previous one, one frame of
progression apart. Those baselines were refreshed with that reason; the underlying lag is
filed as `ore_engine/sky_frame_lag`. Every other scene unchanged.
No hot path of the list changed; `bench: false`.
