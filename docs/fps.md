# Fps

The fps panel: a chart of the last 120 frame times plus the mean, worst and rate of the last
second, at the top right of the surface. Module `box.fps` (`BoxFpsImport`). The panel content is
the script `etc/fps/fps.flecs` (script module `box.fps`), reloaded when the file changes.

## Components

| Name (script) | C type | Fields / role |
|---|---|---|
| `box.fps.Panel` | `FpsPanel` | `open`: shown; a change applies next frame. `interval`: seconds between two updates from engine samples (0.25 by default, 0 or less: every sample; held samples show at once). Singleton, closed by default |
| `box.fps.Summary` | `FpsSummary` | `frames`, `mean_ms`, `worst_ms`, `fps` (1000 / mean) of the last second of samples; 0 without samples. Written only while the panel is open, every `interval`, when what the panel shows of it (frames, times to 0.1 ms, whole fps, color) changed |

Input data is `FrameSamples` (`box.app`, see [app.md](app.md)): a ring of 240 frame intervals in ms, `count`,
`next`, `hold` (engine adds no samples while set, for fixed test data).

Exported script constants: `box.fps.green`, `yellow`, `red` (bar colors) and `fast_ms`
(1000/60 + 0.5 ms vsync tolerance), `slow_ms` (1000/30).

## Open it

```flecs
using box.fps

$ {
  Panel: {open: true}
}
```

Or the shell command `fps` (toggles, returns `true`/`false`), or C:
`ecs_singleton_set(world, FpsPanel, { .open = true })`. `fps <rate>` sets the frame cap instead
(see [command.md](command.md)).

Fixed samples for a capture (from `test/integration/visual/ui_fps.flecs`):

```flecs
using box.app
using box.fps

$ {
  FrameSamples: {interval: [8.3, 9.2, 16.7, 30.0, 52.0], count: 5, next: 5, hold: true}
  Panel: {open: true}
}
```

## Layout and entities

| Path | What |
|---|---|
| `fps.ui` / `fps.view` | Scene root / view, drawn at `BOX_VIEW_ORDER_FPS`: over the editor, under the console |
| `fps.ui.screen` | Layout root that fills the surface (1280x720 points without a surface) |
| `fps.ui.screen.panel` | Instance of template `box.fps.FpsWidget`, 10 pt from the top and right edges |
| `.heading`, `.chart`, `.summary` | Heading text; chart (`bars` + reference lines `line_60`, `line_30`); summary texts `times` (mean/worst) and `rate` |
| `.chart.bars.bar_0` .. `bar_119` | `box.fps.FpsBar` instances, mut `ms` (f64) and `col` (i32) |

Chart: 120 bars, 3 pt wide in 4 pt slots (480 pt), 60 pt high, linear 0..50 ms, height a whole
number of points, at least 3. Green <= 60 fps (+0.5 ms), yellow down to 30 fps, red below.

## Behavior

- Content, view and script are created on first open; closing deletes the view and content.
- The panel takes neither pointer nor keyboard; background, text and border come from `UiTheme`.
- The script is found from the working directory or an ancestor of it. Edit
  `etc/fps/fps.flecs` to change the look; C only writes data.
- Summary window: newest samples up to the first one that brings their sum to 1000 ms (all of
  them when they sum to less).

## Cost

- The bars rotate instead of all being rewritten: a new sample writes `ms`/`col` of one bar (the
  oldest, moved to the column right of the newest) and `first` of `chart.bars`
  (`box.fps.FpsBars`), which shifts every bar left. Neither the chart nor `bars` has a `Layout`,
  so a sample lays nothing out. Every 120 samples that arrive one at a time (or when samples
  change otherwise) columns reset to 0..119. The samples of an interval rotate one bar each and
  write `first` once.
- `bar_N` keeps its name but not its place: it is not always the newest sample.
- A bar that read `FrameSamples` itself would re-evaluate every frame; keep per-sample data in
  the muts written from C.
