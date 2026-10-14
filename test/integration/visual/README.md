# Visual scenes

Every `<scene>.flecs` in this folder is a regression test with a picture instead of an
assertion. `tools/integration.py visual` renders it headless with `boxrun`, compares the frame
with `baseline/<scene>.png` and writes a report page, the same approach as the scene tests of
`flecs-engine`.

```sh
python3 tools/integration.py visual --env <name>                    # every scene
python3 tools/integration.py visual --env <name> primitives views   # some scenes
python3 tools/integration.py visual --env <name> --update-baseline primitives
python3 tools/integration.py visual --env <name> --json tests.json  # bake3 test report
```

## How a scene runs

The runner appends a `visual_capture` entity with a `Surface` of 640x360 (`--size`) whose
`write_to_file` is the absolute path `test/integration/out/<scene>.png`, and runs

```sh
bake3 run apps/boxrun --local-env=<name> -- --headless --frames 3 --timeout 120 --port 0 <scene>
```

A headless run steps with a fixed delta, so the frame it writes is the same run after run. A
run refused with `cpu oversubscribed` is retried. `--app` renders with another bake project and
`--dir` takes scenes and baselines from another folder, which is how a game keeps its own:
`python3 tools/integration.py visual --dir apps/<game>/test/visual --app <game>`.

## Comparison

A scene fails when the difference is strong and widespread: the largest channel delta is above
24 and more than 200 pixels have a channel off by more than 16. GPU tie-break speckle (a few
pixels of any size) and faint drift over the whole frame (every pixel off by 16 or less) pass. A
size change, a failed render and a missing baseline fail. Every scene prints `max`, `mean` and
the count of pixels off by more than 16.

## Report

`test/integration/out/index.html` (`--out` moves it) lists every scene, failures first, with
the baseline, the render and a difference image. The difference is green where the render is
brighter, red where it is darker, yellow where only the hue moved, at half strength where no
channel moved more than 16, over a dimmed grey copy of the baseline where the pixels are
identical. The `.log` of every scene holds the output of its run and `.run.flecs` the script
that ran. The output folder ignores itself in git.

## Rules for a scene

- Self contained: the scene declares its camera, views and content, never a `Surface`, and
  refers to no file by a relative path other than the images in `assets/`, which it names
  from the repository root (`test/integration/visual/assets/checker.png`); a `TexturePath`
  finds them from the working directory of `boxrun` upwards. `assets/gen_assets.py` writes
  the PNGs.
- Static: the camera does not move, and nothing that depends on wall time or history across
  many frames is guarded by a baseline; cover that with a C test.
- Small: a scene renders in well under a second.
- Only the scene a task works on is inspected and its baseline refreshed with
  `--update-baseline`, after looking at the render. Every other scene keeps matching.

## Scenes

| Scene | Covers | What to look for |
| --- | --- | --- |
| `primitives` | Every 3D shape | Box, rounded box, both prisms, sphere, icosphere, capsule in the back row; cone, cylinder, frustum, tube, hemisphere, torus in front, on a grey floor. |
| `views` | Two views, order and clear | A floor, a wall and three columns drawn by a clearing view; a second view created after it, without clear, draws its own `Scene` (a yellow box, a pink ball and a cyan ring) on top with its depth cleared, so even the ball 40 units away covers the columns. |
| `ortho` | Orthographic camera | A 4 by 4 grid of rising blocks seen from an isometric angle with parallel edges and no foreshortening. |
| `instanced` | Instanced batches | 640 static boxes in a color gradient, a row of 24 rotated `DynamicTransform` cubes and a row of 24 `DynamicTransform` spheres in front. |
| `materials_grid` | PBR roughness and metallic | Seven spheres per row with roughness rising from 0 to 1 left to right, metallic 0, 0.5 and 1 from top to bottom: a sharp highlight on the left that spreads and fades to the right, metals darker and tinted. |
| `materials_emissive` | Emissive | Five dark spheres glowing orange with strength 0 to 1, a cyan emissive box and a green emissive ring that stay bright on their shadow side, next to a lit grey box. |
| `materials_blend` | Opacity, blend modes, sort | Red, green and blue panes at half opacity overlapping back to front over a blue pillar, two spheres under a parent with `Opacity` 0.4 (the pink one in front), an additive blue sphere and a multiply yellow slab. |
| `materials_backface` | Back face modes | Two quads and two stars turned away from the camera: the culled ones on the left of each pair are invisible, the `BackFacePlain` ones show their lit back; a yellow strip behind faces the camera. |
| `lights_directional` | Directional lights, ambient | A warm key light from the upper left front and a weak blue fill from the right over a sphere, a red cube and a metal ring on a lit floor, with a dim singleton `AmbientLight`. |
| `lights_point` | Point lights | A red, a green and a blue point light (small glowing spheres) between two columns, a ball and a back wall: colored pools on the floor and wall that fade out at their range. |
| `lights_spot` | Spot lights | A narrow white cone on a pillar, a wide orange cone around a ball and a slanted cyan cone on a cone, each a soft edged ellipse on the dark floor. |
| `lights_many` | 400 point lights, clustering | A 20 by 20 grid of small colored point lights just above a floor with 25 pillars: an even pattern of overlapping colored pools to the far edge, none missing. |
| `textures_tiling` | `TextureWrap`, tiling | A floor tiled 8 by 5 with a red and cream checker, crisp near and smooth far (mip levels). Three quads show the `F` texture at UV -0.5 to 1.5: repeat (four partial tiles), clamp (one `F`, the red top and green left edges stretched out), mirror (the tiles mirrored around the center). |
| `textures_transform` | `TextureTransform` | Emissive quads with the `F` (red bar on top, green on the left): plain, scaled 2 by 1 (two narrow `F`s), offset by a quarter (shifted up left, wrapping), scale -1 (mirrored, green on the right). Below a lit cube, a sphere and a cube tiled 3 by 3 with the texture as albedo. |
| `textures_normal` | Normal, roughness and emissive maps | A flat orange panel next to the same panel with a bump normal map (round bumps lit from the top right), a blue sphere covered in bumps, a metal panel with rough and smooth bands, a dark box with glowing orange rings and a floor of bumps. |
| `textures_svg` | SVG at a requested size | One badge SVG (blue disc, yellow star) loaded at 16 px (blurry), its own 64 px, 512 px and 256 by 128 (squashed), and as the albedo of a lit cube. |
| `scenes_isolation` | Scene isolation | Two `Scene`s in the same space drawn by two views: a warm scene (floor, wall, two pillars) lit only by an orange point light, and a cool scene lit only by a blue sun, whose balls sit on the pillars and whose sphere was reparented out of the warm scene; a nested scene's cube, drawn by a third view, lit green on top and untouched by the blue sun. |
| `shadows_cascades` | Cascaded shadows near and far | A ground plane running to the horizon with a row of red pillars and a row of blue balls further and further away, all casting sharp shadows to the right from the nearest pillar to the far ones, without seams between cascades. |
| `shadows_bias` | Depth bias and normal offset | A tilted ramp with a red cube resting on it, a blue block, a thin yellow slab and a ball on a floor, lit at a low angle: the ramp and floor are clean (no acne stripes) and every shadow starts right at the base of its caster (no peter panning). |
| `shadows_fade` | Shadow fade, two cascades | A field of white pillars on a ground plane with shadows that are dark in front and fade out with distance until the far pillars cast none at 40 units. |
| `shadows_noshadow` | `NoShadow` | Three pillars on a floor: only the red one on the left casts a shadow; the blue `NoShadow` pillar and the green pillar with a ball under a `NoShadow` parent cast none. |
| `scenes_projection` | Cross scene projection | A red capsule and a crate on a floor; a UI `Scene` with an ortho camera (ortho_scale 1) drawn by a second view puts a dark nameplate with a green bar above the capsule and a yellow diamond above the crate, both placed by `ProjectedAnchor`. A magenta plate anchored to a box behind the camera is hidden. |
| `effects_bloom` | Bloom then tonemap | Five orange spheres glowing from strength 0 to 8 with a halo that widens with their strength, a blue bar with a soft glow above them and a dim green box without one (below the threshold), over a dark floor. |
| `effects_tonemap` | Tonemap with auto exposure | A bright sun on spheres from red to yellow, a white cube and a lamp of strength 12: the exposure pulls the frame down (darker blue sky) and the highlights roll off to white without hard clipping. |
| `effects_ssao` | Ambient occlusion | White steps, a pillar, stacked cubes, a ring and a ball on a floor against a wall: soft dark contact shadows where they meet the floor and each other, flat surfaces untouched. |
| `effects_fog` | Height fog | Two rows of pillars running into the distance that fade into a warm fog lit by the sun, a hill behind them half lost in it and a horizon where ground and sky blend. |
