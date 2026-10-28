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
bake3 run apps/boxrun --local-env=<name> -- --headless --frames 3 --timeout 120 --port 0 --delta-time 0.016666667 <scene>
```

The runner passes `--delta-time` (1/60), a fixed step for `ecs_progress`; a headless run without it
measures its delta. With the fixed step the frame it writes is the same run after run. The
runner builds the app once with `bake3 build` and runs every scene with a copy of that binary in
`.bake/integration`, removed when the run ends, so a build in the same environment during the
suite cannot swap the binary halfway. A render the load guard refuses (`cpu oversubscribed`)
waits for the load average to drop to the core count, printing a line when it starts waiting and
every minute, and runs again, up to `--load-wait` seconds (1800) for the whole run; a scene still
refused then fails as `not rendered: cpu oversubscribed`, with its own wait and the run's. When a
commit that changes `etc`, `src`, `include`, `apps` or `test/integration` lands during the run,
the run prints the changed files and keeps them in the history. `--app` renders with another bake project and
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

## History

Every run is also kept in `test/integration/out/history` (`--history` moves it), which a rerun
never overwrites: `<run>/results.json` holds the commit at the start and end, the files that
changed between them, the binary and its build time, and per scene the diff stats, exit code,
attempts, guard refusals, load wait, load average before, at the render and after, render seconds, the error lines of its log and the warnings of headless readiness waits that timed out (`readiness`, also appended to a failing scene's detail); the render, baseline,
diff, log and script of every failing scene are copied next to it. `runs.jsonl` has one summary
line per run with the failing scenes. The last 30 runs are kept.

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
| `materials_cutout` | `AlphaCutoff`, alpha tested shadows | Under a sun from behind on the right: a corner of a lattice fence (`fence.png`, `BackFacePlain`), the left panel showing its textured front and the right one its light plain back, with a blue box seen through the holes of both; a small tree of crossed leaf quads (`leaf.png`) on a trunk; a fence panel with `Opacity` 0.7 drawn as opaque as the others; a leaf with `BlendMode` AlphaBlend whose soft edge blends. Every shape has hard edged holes and nothing behind it sorts wrongly; the fences, the faded panel and the tree cast shadows with the same holes toward the camera, and the blended leaf casts none. |
| `materials_tint` | Tint, hierarchical composition | Five columns of a light blue ball under a box: plain; under a parent tinted blue at half strength (bluer); the box under a child that adds black at half strength (dark blue, the ball stays bluer); the same through `Parent` (identical to the third); an orange tint at full strength over a green ball and a purple box (both orange). In front eight white boxes tinted red from strength 0 to 224. |
| `lights_directional` | Directional lights, ambient | A warm key light from the upper left front and a weak blue fill from the right over a sphere, a red cube and a metal ring on a lit floor, with a dim singleton `AmbientLight`. |
| `lights_point` | Point lights | A red, a green and a blue point light (small glowing spheres) between two columns, a ball and a back wall: colored pools on the floor and wall that fade out at their range. |
| `lights_spot` | Spot lights | A narrow white cone on a pillar, a wide orange cone around a ball and a slanted cyan cone on a cone, each a soft edged ellipse on the dark floor. |
| `lights_many` | 400 point lights, clustering | A 20 by 20 grid of small colored point lights just above a floor with 25 pillars: an even pattern of overlapping colored pools to the far edge, none missing. |
| `textures_tiling` | `TextureWrap`, tiling | A floor tiled 8 by 5 with a red and cream checker, crisp near and smooth far (mip levels). Three quads show the `F` texture at UV -0.5 to 1.5: repeat (four partial tiles), clamp (one `F`, the red top and green left edges stretched out), mirror (the tiles mirrored around the center). |
| `textures_transform` | `TextureTransform` | Emissive quads with the `F` (red bar on top, green on the left): plain, scaled 2 by 1 (two narrow `F`s), offset by a quarter (shifted up left, wrapping), scale -1 (mirrored, green on the right). Below a lit cube, a sphere and a cube tiled 3 by 3 with the texture as albedo. |
| `textures_normal` | Normal, ORM and emissive maps | A flat orange panel next to the same panel with a bump normal map (round bumps lit from the top right), a blue sphere covered in bumps, a metal panel with rough and smooth bands from an ORM map of roughness only, a dark box with glowing orange rings and a floor of bumps. |
| `textures_orm` | ORM map: occlusion, roughness and metallic | A gold panel and a gold ball tiled with `orm.png` under `sky.hdr` and a sun from the upper left. Metal columns mirror the sky sharply in the smooth top half of each tile and blurred in the rough bottom half; the dielectric columns between them are flat matte gold. Dark grooves between the tiles where the sky light is occluded, faint on the sunlit dielectric columns, since the sun is not occluded. |
| `textures_svg` | SVG at a requested size | One badge SVG (blue disc, yellow star) loaded at 16 px (blurry), its own 64 px, 512 px and 256 by 128 (squashed), and as the albedo of a lit cube. |
| `scenes_isolation` | Scene isolation | Two `Scene`s in the same space drawn by two views: a warm scene (floor, wall, two pillars) lit only by an orange point light, and a cool scene lit only by a blue sun, whose balls sit on the pillars and whose sphere was reparented out of the warm scene; a nested scene's cube, drawn by a third view, lit green on top and untouched by the blue sun. |
| `shadows_cascades` | Cascaded shadows near and far | A ground plane running to the horizon with a row of red pillars and a row of blue balls further and further away, all casting sharp shadows to the right from the nearest pillar to the far ones, without seams between cascades. |
| `shadows_bias` | Depth bias and normal offset | A tilted ramp with a red cube resting on it, a blue block, a thin yellow slab and a ball on a floor, lit at a low angle: the ramp and floor are clean (no acne stripes) and every shadow starts right at the base of its caster (no peter panning). |
| `shadows_fade` | Shadow fade, two cascades | A field of white pillars on a ground plane with shadows that are dark in front and fade out with distance until the far pillars cast none at 40 units. |
| `shadows_noshadow` | `NoShadow` | Three pillars on a floor: only the red one on the left casts a shadow; the blue `NoShadow` pillar and the green pillar with a ball under a `NoShadow` parent cast none. |
| `scenes_projection` | Cross scene projection | A red capsule and a crate on a floor; a UI `Scene` with an ortho camera (ortho_scale 1) drawn by a second view puts a dark nameplate with a green bar above the capsule and a yellow diamond above the crate, both placed by `ProjectedAnchor`. A magenta plate anchored to a box behind the camera is hidden. |
| `shapes2d_ui` | 2D shapes in pixel units | On a dark rounded panel with a blue border: a plain, a rounded, an outlined, a pill and a turned rect, a half transparent square; a dot, two discs, a yellow ring, a round badge image with a white border, a rounded `F` image, a checker area and a blue tinted `F`; lines 1 to 8 px wide; one bordered rounded rect at a quarter, half and full scale. Every edge smooth and one pixel soft. |
| `shapes2d_perspective` | 2D shapes in a 3D view | A dark board on the ground with rows of bordered discs, rounded rects and green lines receding to the horizon (sharp near, thin far, no dashes), a tilted card with a badge and two bars, and a purple cube with a white disc label on its face. |
| `shapes2d_scales` | Edges at any scale | One badge (blue rounded card, white border, red ringed disc, dark stroke) at 0.5, 1, 3, 9 and 6 times turned, and a corner of it at 40 times: every edge stays a one pixel transition. |
| `effects_bloom` | Bloom then tonemap | Five orange spheres glowing from strength 0 to 8 with a halo that widens with their strength, a blue bar with a soft glow above them and a dim green box without one (below the threshold), over a dark floor. |
| `effects_tonemap` | Tonemap with auto exposure | A bright sun on spheres from red to yellow, a white cube and a lamp of strength 12: the exposure pulls the frame down (darker blue sky) and the highlights roll off to white without hard clipping. |
| `effects_ssao` | Ambient occlusion | White steps, a pillar, stacked cubes, a ring and a ball on a floor against a wall: soft dark contact shadows where they meet the floor and each other, flat surfaces untouched. |
| `effects_fog` | Height fog | Two rows of pillars running into the distance that fade into a warm fog lit by the sun, a hill behind them half lost in it and a horizon where ground and sky blend. |
| `effects_smaa` | SMAA after tonemap | Five thin leaning poles, a wire across them, four boxes turned at several angles and three spheres against a bright sky over a green ground, with a screen space UI view drawing two lines of dark text top left: every silhouette smooth (no staircases on the poles, the wire or the box edges, a continuous wire), box faces and the sky untouched (no blur inside them), the UI text exactly as crisp as without anti aliasing. |
| `ui_layout` | Layout: fit, grow, align, anchors | A 600 px wide window that fits its height: a title bar (yellow dot, title, two dots pushed right by a grow spacer), three buttons growing 1:2:1, five bars centered and bottom aligned, a list of bars aligned right and a dark stage that stretches to the list height with a red dot top left, a green rect top right, a blue bar bottom left offset by 10, 4, a yellow dot bottom right, a purple pill in the middle and a white dot kept at its own position. |
| `ui_scroll_float` | `LayoutScroll` and `LayoutFloat` | On a dark 640x360 screen: a list scrolled by 70 cut by its clip at the top (a green row) and bottom (a yellow row); its yellow "Choose one" picker opens a green floating list of three options that leaves the list and covers the slate card drawn after it; a strip scrolled sideways cuts its first and last tiles; a blue "Menu" button near the bottom right opens its cream menu upward; a dark tooltip over a yellow dot near the right edge is shifted left to end at the edge. |
| `ui_hud` | Screen space HUD over a 3D view | A lit floor with a crate, a pillar and a red capsule; a HUD `Scene` (ortho camera at ortho_scale 1, a second view without clear, lighting or culling) on top: a translucent status panel with HP and MP bars top left, a minimap with a path under two dots top right, a white crosshair in the middle, an ammo counter bottom right and a "Hero" nameplate placed above the capsule by `ProjectedAnchor`. No element sets z: children draw over parents in hierarchy order. |
| `ui_menu` | Three stacked views | The 3D view, a HUD scene (score and health bar) and a menu scene drawn by a third view: the half black dim covers the world and the HUD, a rounded panel with "Paused" and three buttons (Resume, Settings, Quit) on top. |
| `ui_world_panel` | World space UI | A quest board (title, three rows with a dot, name and count, a progress bar) laid out in logical pixels and scaled to world units on a post, turned toward the camera, and a "Village" sign: text crisp and every child on top of its coplanar parent without flicker; a barrel in front hides part of the scene. |
| `ui_curved_screen` | Render-to-texture UI | A `Surface` with a texture renders a dashboard UI scene (REACTOR, output, a red alert chip and a bar chart) that sixteen emissive quads on an arc sample through `PbrTextures` and `TextureTransform`: one continuous curved screen on a stand, its view not drawn on the main surface. |
| `ui_picking` | Hover and press feedback from synthetic input | A menu panel over a 3D view whose buttons and check box are `Interactive` templates reacting to `on.enter`, `on.leave`, `on.press`, `on.release` and `on.click`. The scene queues input through the command functions: a click on the check box (green mark), a press held on "Pressed" (dark blue, yellow border) and the cursor moved onto "Hover" (light blue); "Idle" keeps its color. |
| `ui_console` | In game console over a 3D view | The default scene (floor, red cube, shadow, effects) under a dark translucent panel covering the left half at full height with a blue line along its right edge, text at the bottom: three lines that ran through `box.console.run` in blue after a `>` prompt, their output (lamp, rope, torch and the cube position) in light grey, a red error line, and an empty input line with a white cursor right after the prompt. |
| `widgets_dark` | The widget gallery of `etc/scenes/widgets.flecs`, dark theme | Every section of the gallery in a 2048x1152 window shown whole: the header with the title and the theme dropdown (Dark), Basics and Text on top, Containers and Form below on dark grey panels. The form's Name field has a blue focus ring, "idg" of "Widget" selected in green and the caret after it; its Shape dropdown is open over the sliders with Box in green and Sphere highlighted. The preview right of the form shows an orange box with a shadow on a grey floor under a blue sky, "Widget" above it. |
| `widgets_light` | The widget gallery, light theme | The `widgets_dark` gallery after the theme dropdown picked Light: light grey background, white panels, dark text, darker green buttons and a blue focus ring, with the same focused field, selection and open dropdown; the preview is unchanged. |
| `culling_occlusion` | Hi-Z occlusion under a fast camera | A city of tall blocks seen from above while the camera flies 50 units a frame (a `CameraController` velocity), so the captured frame shows streets the previous frame hid; every block and every orange ball in the visible streets is drawn, exactly as with frustum culling alone (`occlusion: false`). |
| `shapes_spheres` | Closed round meshes at the town's settings | Up close with the town's view: a pink cherry tree crown of five 8 segment spheres, a green planter ball with pink blossoms on a ribbed pot, a glowing red lantern without dark slits or specks across its faces (its ribs sit inside the sphere) and a flat shaded grey 8 segment sphere, all without gaps. |
| `town_backside` | The town of `etc/scenes/town.flecs` | The 4 by 4 block town from box_old's `town_backside` viewpoint with the view of the etc scene (cascaded shadows, occlusion culling, SSAO, tonemap): a flat roof with laundry and a water tank bottom left, a purple house with a balcony and two cherry trees behind it, a tall timber house on the right with air conditioners and shutters, power lines with sagging wires across the frame and glowing red lanterns in the streets. box_old drew the same layout mirrored left to right and with a black sky. |
| `bake_volume` | Probe volume with ambient initialized probes | A room `Scene` with `LightBake` and a strong blue sky and orange ground `AmbientLight` under a weak sun: a blue pillar against the back wall, a white ball (blue on top, warm below), a red cube, a metal ring, a yellow `NoBake` cube and a pink `DynamicTransform` ball. The baseline is the same scene rendered without `LightBake`, so the scene passes only while the probes match the hemisphere ambient. |
| `particles_forces` | Particle forces and ground collision | Grey smoke from a red chimney on the left blown to the right by a wind through drag, in overlapping puffs that lean further right as they rise; on the right orange and yellow sparks of very different speeds thrown up from a point, some still high, most pulled down by a strong gravity onto the floor where they spread outward in a wide fan. |
| `atmosphere_noon` | Sky and aerial perspective at noon | A blue sky, darker at the top and pale at the horizon, behind a green ground plane running to the horizon and five red boxes from 0.1 to 25.6 km away (`units_per_km` 100) that turn from red to the hazy blue of the horizon with distance. |
| `atmosphere_sunset` | Sky with the sun 3 degrees above the horizon, bloom and tonemap | A dim blue grey sky with an orange and yellow glow around the small white sun disk (`sun_disk_intensity` 20) on the left at the horizon, just right of the nearest box, a darkened ground and five grey boxes from 0.1 to 25.6 km away that fade into the warm haze, the far ones lit gold on their sun side. |
| `atmosphere_sun_disk` | Sun disk with bloom and tonemap | A white sun disk 20 degrees up in the middle of a blue sky, its edge sharp and slightly darker than its center, wrapped in a soft bloom halo. |
| `atmosphere_altitude` | Aerial perspective from 5 km up | Looking down from 5 km over a green plain with red topped boxes: the plain fades into a pale haze toward the horizon and the blue sky above it is deeper than at the ground. |
| `atmosphere_fog_noon` | Height fog colored by the sky at noon | The `atmosphere_noon` view with a `HeightFog` whose color is a pink that never shows: the ground and the boxes fade with distance into a pale blue haze the color of the horizon, the farthest box almost gone, the sky deeper blue higher up. |
| `atmosphere_fog_sunset` | Height fog colored by the sky at sunset, bloom and tonemap | The `atmosphere_sunset` view with a thinner, lower fog of the same pink that never shows: a warm orange haze along the horizon, brightest toward the low sun on the left and fading to a dusky mauve higher up the sky, the boxes turning a pale orange as they fade into it with distance and the farthest almost gone.
| `environment_spheres` | Image based lighting from an environment | Metal balls in the back row and white dielectric balls in front, roughness 0.05, 0.4 and 0.8 from left to right, lit only by `sky.hdr`, which the view draws behind them: a blue sky over a brown ground with a bright horizon band at the middle of the image. The smooth metal ball mirrors sky, horizon and ground sharply, the rougher ones blur them into a blue to brown gradient; the dielectric balls are lit pale blue. |
| `environment_yaw` | Environment yaw | The `environment_spheres` balls lit by `axes.hdr` turned 45 degrees about +Y: the background is yellow on the left half and red on the right half; the smooth metal ball mirrors cyan, blue, green and magenta patches with a red edge, the rough balls blur them, the dielectric balls are lit cyan to purple. |
| `environment_bake` | Light bake next to an environment | On the left a `LightBake` room open toward the camera, lit warm inside by its lamp and the bake, its outer walls black; on the right a mirror ball and a white ball on a slab lit only by `sky.hdr`, which the first view draws behind both. |
| `atmosphere_full_moon` | Full moon at night lighting and shadowing the scene, tonemap | A dark night sky with a faint blue glow and a round, fully lit white moon disk 25 degrees up and off to the side, darkening a little toward its smooth edge; a green ground, a white column, a red ball and a blue crate lit from ahead by the white moonlight, the faces toward the moon bright and the shadows falling back toward the camera. |
| `atmosphere_quarter_moon` | Quarter moon at night, tonemap | The `atmosphere_full_moon` view with the sun 90 degrees from the moon: only the lower half of the disk is lit, the terminator a soft straight line across its middle and the upper half invisible against the sky; the objects are lit and shadowed by the moon as dim as in the full moon view, the sky just as dark. |
| `atmosphere_moon_day` | Moon in the day sky, tonemap | A blue daytime sky with a pale gibbous moon 20 degrees up in the middle, about two thirds of its disk lit, over a green ground with a white column and a red ball lit by the sun from behind the camera. |
