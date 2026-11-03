# Farpost-9: Box feature inventory

Taken from `include/box/*.h` before the scene was designed, and mapped to where the scene uses each
feature. Everything here is driven by Flecs Script. There is no scene-specific C.

## Geometry

| Feature | Where it is used |
| --- | --- |
| Box, RoundedBox | Decks, containers, beams, fins, hull detail |
| Sphere, HemiSphere | Reactor core, tanks, refinery flask, Pleasure Dome |
| Cylinder, Cone, Frustum, Capsule, Tube, Torus | Spine, ducts, burners, Hab Ring, charter gate, deck crowds |
| Lathe | Refinery columns, relay dish |
| Sweep (Path3D with Profile2D) | Cables and overhead conduit |
| ProfileExtrusion (Path2D with Profile2D) | Every chamfered solid: screen bezels, desks, hatches |
| BeveledPrism | Watch post keep, Rim Warden cutter hull |
| Mesh3 | Generated asteroids; chamfered screen fills and neon rings built in templates |
| Rect, Circle, Line, Border, Image | Glass UI line work, feeds, gauges |
| Text (MSDF) with Layout anchoring | All type, from 9 mm labels to neon signage |

## Materials

| Feature | Where it is used |
| --- | --- |
| PbrMaterial with albedo, ORM and normal maps | Stained metal, hull plating, wet deck, rock |
| Emissive with emissive map | Tachyrite veins, under-deck lights, hull windows |
| Transmission | Refinery flask, Pleasure Dome, plinth lens |
| ClearCoat, SpecularAnisotropy | Solar panels and tanks, brushed steel |
| AlphaCutoff with BackFacePlain | Catwalk grating on the hab deck |
| BlendMode Additive, Multiply, AlphaBlend | Neon light, screen darkening, window grime |
| Opacity through the hierarchy | Screens powering up and browning out |
| Transition | Build-out scale, ship flight, asteroid spin, control response |
| TextureTransform | Per-part texture scale and offset so repeats do not line up |

## Rendering

| Feature | Where it is used |
| --- | --- |
| Directional light with cascaded shadows | Sun through the viewport, at room and station scale |
| Point and spot lights | Lamps, strip lights, floods, screen spill, incidents |
| Separate Scene with its own lights | The four security-camera decks |
| Surface render to texture | Exterior feed and CCTV, shown on glass plates |
| FogVolume with noise | Room haze and deck haze |
| ReflectionBake with ReflectionProbe | Wet deck plates |
| EnvironmentImage | Image based lighting from a generated HDR |
| Gtao, Bloom, Tonemap (Tony McMapface), Smaa | Main view chain |
| GPU particles, curves, turbulence, particle lights | Drill spray, welding, stack smoke, fire, gate surge |

## Interaction

| Feature | Where it is used |
| --- | --- |
| Interactive, Cursor, picking on meshes and shapes | Every world-space control |
| on.click, on.drag, on.scroll, on.enter, on.leave | Tabs, faders, dials, hover states |
| async, proc, event, reactive templates | The whole simulation and all UI state |

## Left out on purpose

- Spot and point light shadows: their atlas buffer handle collides with particle pages, see below.
- Atmosphere, TimeOfDay, HeightFog and LightBake: the scene is in vacuum with moving lights.
- Sheen and DiffuseTransmission: nothing in the final design is cloth or thin paper.

## Engine notes found while building this

- `BOX_RENDER_BUFFER_SHADOW_FIRST` and `BOX_RENDER_BUFFER_PARTICLE_FIRST` are both `0x50000000`, so
  shadow casting spot or point lights in a scene with particles corrupt the particle pool.
- A literal `P3` passed to a script function outside a template crashes at load.
- A shape with no Position, Rotation or Scale renders at the world origin, ignoring its parent.
- `Input.mouse.normalized` stays zero in a headless run with a pinned cursor; `position` works.
- A render-to-texture surface whose only view has no camera shows black, not its last frame.
