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
  refers to no file by a relative path.
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
