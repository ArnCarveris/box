# Surface

`box.surface` (`BoxSurface`, `include/box/surface.h`) turns a `Surface` component into a render
target: a platform window, or an offscreen texture (headless runs, or a surface with a `texture`).
Setting a `Surface` also switches the world to GPU rendering.

## Surface

| Field | Default (ctor) | Notes |
| --- | --- | --- |
| `width`, `height` | 1280, 720 | Requested size in points, clamped to 1..8192 |
| `resolution_scale` | 1 | Divides the render pixels (2 = half resolution, upscaled) |
| `pixel_ratio` | 1 | Offscreen pixels per point; 0 or less is 1. Windows use the display's ratio |
| `vsync` | false | Present with vertical sync |
| `anisotropy` | `AnisotropyDefault` (4x) | `AnisotropyOff/Low/Medium/High/Ultra` = 1/2/4/8/16 samples |
| `texture_quality` | `TextureQualityDefault` (4096) | `TextureQualityLow/Medium/High/VeryHigh` = 512/1024/2048/4096 cap; changing it reloads textures |
| `texture` | 0 | Texture entity materials sample this surface as; makes it offscreen |
| `view_root` | 0 | Parent of the views it renders; 0 renders the views no other surface claims |
| `write_to_file` | NULL | PNG written every frame; relative paths go to `AppStatus.tmp_dir` |

`SurfaceStatus` (script `box.surface.Status`, engine written, read only): `width, height` (presented
pixels), `point_width, point_height` (the size cameras and anchors use), `render_width,
render_height` (after `resolution_scale`), `window` (presents to a window).

```flecs
using box.surface

window {
  Surface: {width: 1280, height: 720, vsync: true}
}
```

`SurfaceSchedule` (script `box.surface.Schedule`, on a surface with a `texture`): `interval` (s between renders, 0 every frame),
`seen` (entity: render only while a perspective view no texture surface renders has the sphere of `radius`
about its world position in its frustum, within `range` of its eye and, with portal culling, in an
area its camera saw), `paused` (render nothing). Between renders the texture keeps its last image
and the surface's views cost the GPU nothing; it renders once at the start regardless.
`SurfaceScheduleStatus` (script `box.surface.ScheduleStatus`, read only): `seen`, `renders`. A camera feed on a screen:
`Schedule: {interval: 0.066, seen: screen, radius: 1, range: 12}`.

Render to texture (a UI screen on a 3D object):

```flecs
screen_texture {}

screen_views {
  view { View: {camera: screen_ui.camera, clear: {enabled: true, color: {0, 0, 0, 255}},
                lighting: {enabled: false}, culling: {enabled: false}} }
}

screen_surface {
  Surface: {width: 512, height: 160, texture: screen_texture, view_root: screen_views}
}

screen {
  Quad: {4, 1.25}
  PbrTextures: {albedo: screen_texture, emissive: screen_texture}
}
```

## Rules and gotchas

- Views render by `View.order`, then creation. A surface renders the views under its `view_root`;
  surfaces without one render every view not under another surface's `view_root`. Give a texture
  surface a `view_root` and put its views under it, else both surfaces render all unclaimed views.
- A surface with a texture renders before the others; materials sampling the texture entity see that
  frame's image, never their own surface inside itself. Pick rays continue through a texture surface
  at the hit UV (`docs/picking.md`).
- The texture entity has no `Texture` component; its size is `SurfaceStatus.width x height`, 0 until
  the surface rendered once (`box_surface_texture_size`).
- Changes reconfigure the target before the next frame. A window resize only updates
  `SurfaceStatus`; read sizes with `box_surface_point_size`, not `Surface.width`.
- No surface: with `--surface WxH` the app creates one (vsync on) on the first frame, else gives the
  first window surface that size; later writes keep the command line size.
- Closing a window quits the world (`box.surface.Poll`).
- If a window cannot open, the surface renders offscreen with a warning. In the browser the page
  canvas is the window and the surface follows its size.

## Systems

| System | Phase | Does |
| --- | --- | --- |
| `box.surface.FromApp` | PreFrame (once) | Applies `--surface` |
| `box.surface.Poll` | PreFrame | Processes window events, quits on close |
| `box.surface.Extract` | RenderExtract | Describes every surface in the render packet, releases removed ones |

## C API

| Function | Does |
| --- | --- |
| `box_surface_point_size(&s, &status, &w, &h)` | Window size from status when known, else requested size (clamped) |
| `box_surface_texture_size(world, texture, &w, &h)` | Texel size of a surface texture; false if none renders into it yet |
| `box_surface_of_view(world, query, view)` | Surface a view renders to (NULL query makes one) |
| `box_surface_view_size(world, query, view, &w, &h)` | Point size of a view's surface, else 1280x720 |

## Command line and shell

```sh
bake3 run apps/boxrun --local-env -- --surface 1920x1080 etc/scenes/town.flecs
bake3 run apps/boxrun --local-env -- --headless --frames 3 --delta-time 1/60 --out /tmp/town.png etc/scenes/town.flecs
```

- `--headless`: every surface is offscreen (a point is a pixel at `pixel_ratio` 1).
- `--out <file>`: image of the first window surface (no texture).
- `capture <surface>`: PNG of a surface with all its views.
