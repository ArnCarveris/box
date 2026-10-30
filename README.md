# Box

A data-driven 2D/3D game framework built on [Flecs](https://github.com/SanderMertens/flecs)
and WebGPU.

## Prerequisites

- [bake3](https://github.com/SanderMertens/bake3)
- flecs checked out at `../../flecs` and built once with `bake3 ../../flecs`
- A Rust toolchain

## Build

```sh
bake3 --local-env
```

## Run

```sh
bake3 run apps/boxrun --local-env -- etc/scenes/town.flecs
```

## Test

```sh
bake3 run test/cpu/core --local-env -- -j 8
python3 tools/integration.py visual
```

## Commands

Box uses the [Flecs shell][shell] to inspect and drive a running app. Lines typed in the in-game
console (the grave key) run in a shell session, and the REST port (`--port`, 27750 by default)
serves the shell endpoints:

```sh
curl -X PUT localhost:27750/shell/session                  # {"session":<id>}
curl -X PUT localhost:27750/shell/session/<id> -d "help"   # run a command line
```

Next to the builtin commands of the shell (`ls`, `cd`, `get`, `set`, `query`, the script
commands, ...) box adds `key_press`, `mouse_click`, `action_press` and the other input commands,
`capture` and `render` (PNG resources, fetched with
`GET /shell/session/<id>/resource/<name>`), `fit`, `fps`, `frames`, `edit` and `logs`. `help`
lists them all. `fit [camera]` moves the active camera, or a camera, along its view until it
tightly frames every mesh of its scene, keeping its direction. `fps` shows or hides a chart of
the recent frames at the top right (green at 60 fps or faster, yellow under 60, red under 30)
and `fps <rate>` caps the frame rate, `fps 0` removes the cap; `frames` prints the frame timing. `tools/box_client.py` drives an app over these endpoints
for the e2e tests.

[shell]: https://github.com/SanderMertens/flecs/blob/master/docs/FlecsShell.md

### Console

The in-game console keeps one shell session. Its prompt is bash-like: the scope of the session
(`~` at the root, otherwise the path that `cd` moved to) followed by `$`, and every line in the
scrollback keeps the prompt it ran at. The scope, the `$`, the parts of command lines and the
history suggestion take their colors from `Console.syntax`. `cls` clears the scrollback. A script
runs a line with `box.console.run(line)`, types into the input line with
`box.console.type(text)` and completes it as Tab does with `box.console.complete(back)`.

Tab completes the bare word before the cursor, like the terminal of the flecs explorer: the first
word to a command name (the shell builtins, the box commands and `cls`, with their description),
the topic of `help` to a command name or the `console` help group, and an argument whose
parameter takes an entity or id (or is named `script`) to the names of the children of the
session scope. A dotted word (`a.b.c`) completes from the children of its parent part, resolved
like the shell resolves it. Words inside a string or a value expression, and other arguments,
do not complete.

One candidate is inserted with a space after it, or a `.` for an entity with children. Several
insert their common prefix; when that adds nothing the candidate list opens under the prompt,
colored by kind (command, entity, module, component) and wrapped at the panel width, with a
dimmed `... N more` past 24 candidates. Tab and Shift+Tab then cycle through the list and insert
the active candidate, Enter accepts it, typing closes the list and keeps the text, and Escape
closes the list. When nothing completes the prompt flashes. Completion only reads the children
of one parent and collects at most 256 candidates, so it stays cheap in large worlds.

| Key | Action |
| --- | --- |
| Enter | Run the line, or accept the active candidate of the completion list |
| Tab / Shift+Tab | Complete the word before the cursor, cycle forward / back through the candidate list |
| Up / Down | Walk the lines that ran that start with the text typed before the first Up, Down past the newest restores that text |
| Right / End | With the cursor at the end, accept the dimmed suggestion: the rest of the newest line that ran that starts with the input |
| Ctrl+A / Ctrl+E | Move to the start / end of the line |
| Ctrl+U / Ctrl+K | Delete to the start / end of the line |
| Ctrl+W | Delete the word before the cursor |
| Ctrl+C | Abandon the line: it is echoed with `^C` and nothing runs |
| Ctrl+L | Clear the scrollback, keeping the input line |
| Ctrl+V | Paste |
| Page Up / Page Down, wheel | Scroll the scrollback |
| Escape | Close the candidate list when it is open, otherwise the console |
| Grave | Close the console |

While the console is open it takes the keyboard, so none of these keys reach the actions of the
app; only the toggle still sees its key.
