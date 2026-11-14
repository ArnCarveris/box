# Startup

`box.startup` (`src/startup`, `include/box/startup.h`) shows a centered panel listing the tasks
of `InProgressTask` (see [task.md](task.md)) with their progress while an app starts and a
blocking task runs, so a long first load (cold shader compiles, fonts, textures) reads as
progress instead of a frozen window.

## Component

| Component (script name) | Fields | What it does |
|-------------------------|--------|--------------|
| `StartupOverlay` (`box.startup.Overlay`) | `enabled`, `disabled`, `open`, `finished` | Singleton. `enabled`: may open in any world (tests). `disabled`: never opens (an app with its own progress UI). `open`, `finished` are written by box.startup |

## Behavior

- Eligible when not `disabled` and either `enabled`, or `box_app_run` is running with a window
  (not `--headless`) and writes no capture (no `--out`).
- Opens while `InProgressTask.blocking > 0`. Once `BOX_STARTUP_FRAMES` (10) frames in a row have
  no blocking task, it closes and `finished` is set: it never opens again, so later background
  work (a rebake after an edit) doesn't bring it back. If no blocking task appears in the first
  10 frames it never opens.
- Decided by `box.startup.Update` in `EcsOnLoad`.
- Content is created on first open and deleted on close (the script stays loaded):
  - `startup.ui`: scene root; `startup.ui.screen` fills the surface; `startup.ui.screen.panel`
    is an instance of the script template `box.startup.StartupWidget`.
  - `startup.view`: view without clear, lighting, culling or effects at
    `BOX_VIEW_ORDER_STARTUP` (1750000): over game views, editor and fps panel, under the console.
- The panel is sized to the first Surface that doesn't render to a texture.

## Script

The panel is defined in `etc/startup/startup.flecs` (module `box.startup`), located in the working
directory or an ancestor, watched and reloaded when it changes. A missing file warns and leaves
the panel empty; it is looked for again the next time the overlay opens. It reads the tasks straight from the singleton:

```flecs
module box.startup
using box.task
using box.ui

template StartupWidget {
  prop title: string = "Starting"
  const t = box.ui.theme[Theme]
  const tasks = InProgressTask[InProgressTask]

  for i in 0..tasks.count {
    "task_{i}" {
      const task = tasks.tasks[i]
      // name, "{task.done} / {task.total}" when total > 0, and a progress bar
    }
  }
}
```

Children of the panel: `heading` (Text) and `task_0`..`task_N` (one per task, blocking first),
each with `name` and `progress` Texts.

Disable it from a scene:

```flecs
$ {
  box.startup.Overlay: {disabled: true}
}
```

## C API

| Function | Use |
|----------|-----|
| `box_startup_script(world, path)` | Use another overlay script (relative path, found like `etc/...`); `NULL` restores `BOX_STARTUP_SCRIPT_PATH`. Applies the next time it opens without its script loaded |

Tests load `test/assets/startup/startup.flecs` (a copy) with `box_startup_script` and set
`StartupOverlay.enabled`, since a test world isn't a windowed `box_app_run`.
