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
`GET /shell/session/<id>/resource/<name>`), `fps`, `frames`, `edit` and `logs`. `help` lists them
all. `tools/box_client.py` drives an app over these endpoints for the e2e tests.

[shell]: https://github.com/SanderMertens/flecs/blob/master/docs/FlecsShell.md

### Console

The in-game console keeps one shell session. Its prompt is bash-like: the scope of the session
(`~` at the root, otherwise the path that `cd` moved to) followed by `$`, and every line in the
scrollback keeps the prompt it ran at. The scope, the `$`, the parts of command lines and the
history suggestion take their colors from `Console.syntax`. `cls` clears the scrollback. A script
runs a line with `box.console.run(line)` and types into the input line with
`box.console.type(text)`.

| Key | Action |
| --- | --- |
| Enter | Run the line |
| Up / Down | Walk the lines that ran that start with the text typed before the first Up, Down past the newest restores that text |
| Right / End | With the cursor at the end, accept the dimmed suggestion: the rest of the newest line that ran that starts with the input |
| Ctrl+A / Ctrl+E | Move to the start / end of the line |
| Ctrl+U / Ctrl+K | Delete to the start / end of the line |
| Ctrl+W | Delete the word before the cursor |
| Ctrl+C | Abandon the line: it is echoed with `^C` and nothing runs |
| Ctrl+L | Clear the scrollback, keeping the input line |
| Ctrl+V | Paste |
| Page Up / Page Down, wheel | Scroll the scrollback |
| Escape, grave | Close the console |

While the console is open it takes the keyboard, so none of these keys reach the actions of the
app; only the toggle still sees its key.
