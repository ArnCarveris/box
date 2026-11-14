# App

`box.app` (`src/app`, `include/box/app.h`) owns the process: the `App` settings the command line
fills, the frame loop (`box_app_run`), headless runs, frame timing, file watching and `SetAt`.
`apps/boxrun` is the generic runner (native and browser); `apps/boxweb` is the browser engine
that `tools/publish` ships.

## Run

```sh
bake3 run apps/boxrun --local-env -- etc/scenes/town.flecs                        # window
bake3 run apps/boxrun --local-env -- --headless --frames 10 etc/scenes/town.flecs  # no window
bake3 run apps/boxrun --local-env -- --headless --frames 3 --delta-time 1/60 --out /tmp/town.png etc/scenes/town.flecs
bake3 run apps/boxrun --local-env -- --headless --timeout 60 --port 27760 etc/scenes/town.flecs  # REST shell
bake3 run apps/boxrun --local-env=agent_a -- etc/scenes/town.flecs                # per-agent build env
```

`boxrun` appends `.flecs` to a scene without that extension and names the world after it. It
exits 1 when parsing, starting or the run fails.

## Command line (`box_app_parse`)

| Option | App field | Notes |
|--------|-----------|-------|
| `scene.flecs` | `scene` | One positional scene; a second one is an error |
| `-h`, `--help` | `help` | Prints usage, exits 0 without running |
| `--headless` | `headless` | No window; needs `--frames` or `--timeout` |
| `--frames <n>` | `frames` | Stop after n frames, n >= 1 |
| `--timeout <s\|infinite>` | `timeout` | Stop after s wall seconds; `infinite` = -1 |
| `--fps <n>` | `fps` | Frame cap, 0 = none; overrides scene `FpsTarget` |
| `--delta-time <s>` | `delta_time` | Fixed delta per frame, `0.016` or `1/60`; 0 = measured |
| `--surface <w>x<h>` | `surface_width/height` | 1..8192; resizes the first window Surface or creates one |
| `--out <file>` | `out` | Writes the surface to a PNG (`.png` appended when no extension) |
| `--camera <name>` | `camera` | Surface shows this Camera; must exist after the scene loads |
| `--design` | `design` | Gallery of 6 ortho + 2 perspective views; not with `--camera` |
| `--port <n>` | `port` | REST command port, default 27750, 0 disables |
| `--cpu-budget <pct>` | `cpu_budget` | `50` or `50%` of one core, default 100 (headless only) |
| `--unguarded` | `unguarded` | Start headless even on an oversubscribed machine |
| `--async-loads` | `async_loads` | Headless frames don't wait for loads (timing like a window) |
| `--fail-on-error` | `fail_on_error` | Exit 1 when any error was logged during the run |
| `--no-audio` | `no_audio` | Open no audio output at all |
| `--gpu <value>` | - | Accepted and ignored |

`App.unpaced` (ignore CPU budget) has no flag; set it from C.

## Components

| Component (script name) | Key fields | What it does |
|-------------------------|------------|--------------|
| `App` (`box.app.App`) | see table above | Process settings; `delta_time` and `fps` are read every frame, so changing them via REST/script applies next frame |
| `FpsTarget` (`box.app.FpsTarget`) | `value` | Scene frame cap singleton; `App.fps > 0` wins |
| `AppStatus` (`box.app.Status`) | `frame`, `elapsed`, `delta_time`, `slept`, `tmp_dir`, `running`, `errors` | Written by the engine, read only |
| `FrameTiming` | `delta_ms`, `interval_ms`, `delta/interval/error/present` (`FrameRange` min/max/mean), `late`, `dropped`, `total*`, `vsync`, `target_ms` | Last 240 frames (`BOX_FRAME_WINDOW`), written in PreFrame |
| `FrameSamples` | `interval[240]`, `count`, `next`, `hold` | Ring of frame intervals; set `hold` to freeze fixed samples (tests, captures) |
| `FileWatch` | `interval` | Seconds between file stat passes, default 0.25, 0 every frame, < 0 off |
| `FileWatchStatus` | `files`, `polls`, `changes`, `last` | What the watch did |
| `SetAt` | `frame`, `entity` | Copies the other components of its entity onto `entity` once `Status.frame` reached `frame`, then deletes itself |

Late frame: `|delta - interval| > 1 ms`. Dropped frame: interval > 1.5 x target interval.

## Script snippets

```flecs
using box.app

// cap the scene at 60 fps (--fps overrides)
$ {
  box.app.FpsTarget: {60}
}

mover { Position: {0, 0, 0} }

// at frame 30, set Position of mover (write SetAt FIRST in the body)
change_mover {
  SetAt: {frame: 30, entity: mover}
  Position: {150, 0, 0}
}

// wall seconds since start, in an expression
const elapsed = box.app.Status[box.app.Status].elapsed
```

## SetAt rules

- Only components *written* (set / script assignment) after `SetAt` are copied. Pairs, tags,
  `SetAt` itself and components only added (e.g. via `With`) are not.
- A component present before `SetAt` is copied only when it differs from its constructed value;
  it warns when it can't tell. Hence: `SetAt` first.
- The `SetAt` entity gets `Disabled`, so it never matches normal queries.
- Applied at the start of the frame (PreFrame, `SetAtUpdate`).

## Headless runs

- Must have `--frames` or `--timeout` (else refused).
- Refused when the 1 min load average plus the budget's cores exceeds the core count
  ("cpu oversubscribed ..."); `--unguarded` skips this guard.
- Sleeps (no busy wait) when it used more CPU than `--cpu-budget` since the previous frame;
  `AppStatus.slept` totals it. `App.unpaced` disables pacing.
- Gets a private temp folder `/tmp/box/<pid>` (`AppStatus.tmp_dir`), removed when the last world
  using it is deleted. `/tmp/box` must be a real folder owned by the user or root (sticky if
  others can write).
- Deterministic by default: frames wait for texture, TextureGen, font/glyph, sound and file
  watch work, pipelines wait for their compile, and all light probes / reflections bake in one
  frame. `--async-loads` turns the load waits off.
- Audio uses the null transport (renders deltas, no device). With a fixed `--delta-time` two
  runs capture identical audio.

## Run outcome

`box_app_run` returns nonzero when the run was refused or failed to start, when `App.scene` is
set and the scene is still not loaded at the end (a scene that failed and loaded later passes),
or with `--fail-on-error` when any error was logged. A scene that fails to load is logged and the
world still runs.

## C API

| Function | Use |
|----------|-----|
| `box_app_parse(world, argc, argv)` | Fill `App` from the command line |
| `box_app_run(world)` | Load scene, create views, open REST and audio, loop `ecs_progress(world, App.delta_time)` |
| `box_app_fps(world)` | Effective cap: `App.fps` if > 0, else `FpsTarget` |
| `box_app_locate(path)` | Find a relative path in cwd or an ancestor (so `etc/...` resolves from subfolders); caller frees |
| `box_frame_samples_add` / `box_frame_sample(samples, age)` | Write/read `FrameSamples` (age 0 = newest) |
| `box_wait_until(deadline)` | Precise OS sleep until an `ecs_os_get_time` deadline |

Minimal app (`apps/boxweb/src/main.c`): `ecs_init`, `ECS_IMPORT(world, Box)`, `box_app_parse`,
then `box_app_run` unless it failed or `App.help` is set, then `ecs_fini`.

## Gotchas

- No window and no render without a `Surface`: only the scene, `--surface`, `--out`, `--camera`
  or `--design` create one (true in the browser too).
- Audio output opens only in `box_app_run`; a world that just imports `Box` opens no device.
- REST is on by default (port 27750): give concurrent runs distinct `--port`s or `--port 0`.
- `--out` is resolved against the working directory; with `--design` each view also writes
  `<out>-top.png`, `-bottom`, `-left`, `-right`, `-front`, `-back`, `-perspective`,
  `-perspective-back`. Design mode replaces the scene's lights with a dim ambient light.
- File watching reloads shaders, `TexturePath`, `Font` and `EnvironmentImage` files when they
  change on disk; old content stays until the new load is ready.

## Web build

```sh
bake3 --target em --local-env=<name> apps/boxrun   # boxrun.html/.js/.wasm, embeds all of etc
python3 tools/publish etc/scenes/castle              # boxweb (release, embeds a subset of etc) + game files
```

- Output: `$(tools/integration.py local-env --env <name>)/wasm32-Emscripten/debug/bin`
  (`.bake/local_env/<name>/...`, or under `output_dir` when bake3's `bake.json` sets one).
- `boxrun.html#<args>`: the URL hash holds the command line, space separated, e.g.
  `boxrun.html#--fps 30 etc/scenes/town.flecs`. `?preload=<path>` (repeatable) fetches a file
  from the server into the same path of the page's file system before `main` runs.
- `boxweb` embeds `etc/{shaders,fonts,luts,environments,assets,fps,startup,editor}`; the publish
  loader fetches the scene and its folder (`etc/scenes/<scene>/`) into the file system.
- Needs WebGPU and wasm SIMD (`-msimd128`, enforced at compile time). No threads: jobs and file
  watch run inline. No REST port: run commands with
  `Module.cwrap('box_command_web_run', 'string', ['string'])`. The fps cap skips animation
  frames instead of sleeping. No process CPU time, so CPU budget pacing does nothing.

## Browser checks (`test/integration/web`)

Each `<check>.mjs` serves the built page on a free localhost port, opens `<check>.flecs` via
`?preload=` at a fixed delta of 1/60 s in headless Chrome, takes screenshots and fails on any
logged error or invalid WebGPU object. Exit 0 pass, 1 fail, 2 Chrome/page could not start.

```sh
node test/integration/web/<check>.mjs [--chrome <path>] [--out <dir>] \
    "$(tools/integration.py local-env --env <name>)/wasm32-Emscripten/debug/bin"
```

Chrome comes from `--chrome`, `$CHROME`, else the macOS install; `--out` keeps screenshots.

| Check | Covers |
|-------|--------|
| `shader_error` | Breaks `etc/shaders/mesh.wgsl`: one compile error with line, scene keeps rendering |
| `transitions`, `animation` | Transitions / keyframe animation on the main thread |
| `transmission` | Rough glass never dark before the transmission shader compiles |
| `particles` | Particle curves read from a vertex-stage storage buffer |
| `wireframe` | Wireframe without line polygon mode, device without optional features |
| `texture_blend` | `TextureBlend` pipeline variant and texture arrays |
| `text_variants` | Bold/italic variants of `DefaultFont` load from embedded `etc` |
| `lod` | Levels of detail picked by the cull pass |
| `audio_core` | AudioWorklet mixer: gesture start, levels, volume ramp, main thread stall, events |
| `audio_core_safari` | Same in real Safari via `safaridriver` (`--safaridriver`/`$SAFARIDRIVER`) |
| `audio_module`, `audio_spatial`, `audio_reverb`, `audio_synth` | `box.audio` module, panning, reverb, synth sounds |
| `audio_sample`, `audio_stream`, `audio_all_features` | WAV/FLAC decode, Ogg streaming, everything together |

- Audio checks need a trusted click (DevTools `Input.dispatchMouseEvent`) to start audio.
- Safari: run `sudo safaridriver --enable` once and enable "Allow remote automation" in Safari's
  Developer settings; needs a logged-in GUI session.
- macOS quarantine: a quarantined Chrome `.app` (e.g. Homebrew) hangs headless at its first launch
  check, so the scripts fail fast with exit 2. Open Chrome once from Finder, run
  `xattr -dr com.apple.quarantine <app>`, or pass `--chrome` with an unquarantined copy.
  `test/integration/web/quarantine.sh` tests this check.

## Other runners

`tools/integration.py` drives `boxrun`: `visual` (render scenes, compare baselines,
`--update-baseline <scene>`), `perf` (frame times, release), `e2e` (scenarios over the command
port), `local-env` (print the bake3 env folder).
