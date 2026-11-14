# Command

Box commands for the flecs script shell: captures, renders, camera fit, frame timing, the editor,
logs, geometry and audio inspection, and synthetic input. Module `box.command`
(`BoxCommandImport`); commands are script functions in `box.command.commands`, which is put at
the front of `EcsScriptShell.command_paths`, so a box command overrides a flecs builtin of the
same name. Everything else (`ls`, `get`, `set`, `query`, `tree`, `help`, `quit`, ...) is the
flecs shell's own. `help` lists every command, `help <cmd>` its parameters.

## Where commands run

| Entry point | How |
|---|---|
| REST port | `--port <n>` (default 27750, 0 disables). `PUT /shell/session` opens a session (`{"session": id}`), `PUT /shell/session/<id>` with the line as body runs it, `GET /shell/session/<id>/resource/<name>` fetches a resource |
| In game console | Grave key, see [console.md](console.md) |
| Browser | `Module.cwrap('box_command_web_run', 'string', ['string'])(line)`: runs in one lazily created session of the first world that imported `box.command`; output as `box_command_run` formats it, valid until the next call |
| C | `ecs_shell_session_init` + `box_command_run(world, session, line, &error)` |
| Python | `tools/box_client.py` (`App(binary, args).start()`, `exec`, `get`, `wait_input`, `wait_for`, `capture`, `quit`); used by `tools/integration.py e2e` |

```sh
bake3 run apps/boxrun --local-env -- --headless --timeout 60 --port 27760 etc/scenes/town.flecs
curl -X PUT localhost:27760/shell/session                       # {"session": ...}
curl -X PUT localhost:27760/shell/session/<id> -d 'capture'    # returns a resource
curl localhost:27760/shell/session/<id>/resource/<name> -o out.png   # empty until written
```

## Commands

| Command | Returns / does |
|---|---|
| `capture [camera\|surface]` | PNG resource of the active camera (as its view renders it, effects and view debug drawing included), a camera, or a surface as presented with all its views. Size: the surface of the camera's first view |
| `render <subject>` | PNG resource (512x512) of an entity, prefab or template alone in a private scene: prefab instantiated, template added, entity copied; plain lit, no shadows or effects, camera fitted |
| `fit [camera]` | Moves the camera (default: active) along its view to the tightest fit of the meshes of its scene (90% fill, centered). Raises a too-near far plane, never lowers it; `far_: 0` stays 0. Ortho: sets `ortho_height`. Prints `fit <cam> to N entities, center (...), distance D` |
| `fps [rate]` | No arg: toggle `box.fps.Panel.open` (returns `true`/`false`). With rate: sets `App.fps` (overrides `FpsTarget`), 0 = no cap |
| `frames` | Rate, cap, vsync; last/min/mean/max of delta, interval, error, present; late (`\|delta - interval\| > 1 ms`) and dropped (> 1.5 intervals) counts; render packets in flight |
| `edit [on\|off]` | Toggle or set `Editor.open`, returns whether open |
| `logs [level] [count]` | Last `count` (20) captured log messages at `level` (`fatal`, `error`, `warning`, `info`, `debug` or a number) or worse; keeps 1024 |
| `geometry` | Shape, triangle and cached mesh counts |
| `geometry.check [root] [tolerance=u] [area=u^2] [max=n] [hidden]` | Lists pairs of 3D shapes whose faces z-fight (coplanar, same facing, overlapping); defaults 0.0001 / 0.0001 / 100 |
| `audio`, `audio.*` | Audio state and the mixer/transport/analysis subcommands: `volume mute mute_bus solo bypass note play stop seek tempo song capture render analyze diff plot meter notes chords check`; see [audio.md](audio.md) |

## Input commands

All queue onto the synthetic input queue of `box.input` (fail when it is not imported) and return
the frames until the queue is drained. `dur` is in frames (default 1, max 36000); walks take at
most 1000 steps.

| Command | Does |
|---|---|
| `key_press <key> [dur]` | Tap a key |
| `key_down <key>` / `key_up <key>` | Hold / release |
| `key_repeat <key> [count] [dur]` | Repeat a held key (OS autorepeat) |
| `key_type <text> [dur]` | One key press per character, with typed text |
| `mouse_move <pos> [norm]` | Pin the cursor at `[x, y]` window points (top left origin), or 0..1 with `norm true` |
| `mouse_sweep <from> <to> [steps] [dur]` | Walk the cursor (10 steps default) |
| `mouse_release` | Unpin, the real pointer moves the cursor again |
| `mouse_button <button> <down>` | `left`/`middle`/`right`, `true` presses |
| `mouse_click <button> [pos] [dur]` | Click at pos (`-` or omitted: the cursor) |
| `mouse_drag <button> <from> <to> [steps] [dur]` | Hold a button while walking |
| `mouse_scroll <dx> <dy>` | Wheel |
| `action_press <action> [dur]`, `action_down`, `action_up` | Hold an `Action` by path or name |
| `action_drag <action> <from> <to> [steps] [dur]` | Hold an action while walking |
| `action_list` | Actions: path, `(down)` when held, description |
| `input_wait` | Frames until the queue drains; poll until `0` |
| `input_reset` | Empty the queue, release keys, buttons, actions, cursor |
| `input_focus <bool>` | Gain/lose focus; platform input is ignored without it |
| `clipboard_set <text>` / `clipboard_get` | Clipboard the paste shortcut reads |
| `press`, `release`, `click` | flecs' entity pointer commands, overridden so the events also drive the entity's `ActionTrigger` through `box.picking` |

Key names are case insensitive: `Key` constants (`W`, `Space`, `MouseLeft`), DOM key values
(`ArrowLeft`, `Control`) or a single character (`a`, `1`, `/`). Positions parse as `[10, 20]`,
`{10 20}` or `10,20`.

```
mouse_move [166, 298]
mouse_click left
key_type "hello"
input_wait
get $ box.app.Status.frame
```

## Resources

- `capture`, `render`, `audio.capture`, `audio.render`, `audio.plot` create an empty resource
  (`flecs.script.shell.resource`: name, content type) and return it at once. The file is replaced
  in one rename once the GPU (or offline audio world) finished, so a client sees empty or whole:
  poll the resource endpoint until it is non-empty.
- Native: a session's resources live in `/tmp/box/<pid>/shell/<session>` (not flecs'
  `.flecs/shell/<session>`, which processes in one directory would share). The folder is removed
  when the last world using it is deleted. `box_command_resource_file(world, session, name)` gives the path.
- Resources need a session: a command run outside one logs an error.
- Browser: `capture`/`render` fail ("captures are not available in the browser");
  `audio.capture`/`render`/`plot` write `/tmp/audio_capture_<n>.wav`, `/tmp/audio_render_<n>.wav`,
  `/tmp/audio_plot_<n>.png` in the page file system and return that path.

## C API

| Function | Notes |
|---|---|
| `box_command_run(world, session, line, &error)` | Runs a line, one formatted result per line. If any error was logged the output is only the errors, each prefixed `BOX_COMMAND_ERROR` (`"error: "`). Caller frees |
| `box_command_value(&shell_result)` | Value of the last command of an `ecs_shell_run` result |
| `box_command_format(world, &value)` | String as is, entity as path, `help` info as usage, vector/array one element per line, else JSON |
| `box_command_resource_file(world, session, name)` | File of a session resource, NULL without one |

Adding a command (in `src/command`): `box_command_register(world, brief, (ecs_function_desc_t){
.name, .params, .return_type, .callback })`; the brief is what `help` shows. Register from the
`box_cmd_*_init` called in `BoxCommandImport`. `box_command_scope(world)` is the session's `cd`
scope.

## Gotchas

- A command that logs an error still returns a value; check `error` / the `errors` of the REST
  reply, not the value.
- `get` and `set` of the shell take whole components; `box_client.py` `exec` adds member paths
  (`get e Position.y`, `set e View.shadows.enabled false`).
- `fps`/`edit` look up `box.fps.Panel`/`box.editor.Editor`; they fail when those modules are
  not imported.
- Run headless with a fixed `--delta-time 1/60` for reproducible captures and audio renders.
