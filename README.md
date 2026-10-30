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
all. `fps` shows or hides a chart of the recent frames at the top right (green at 60 fps or
faster, yellow under 60, red under 30) and `fps <rate>` caps the frame rate, `fps 0` removes the
cap; `frames` prints the frame timing. `tools/box_client.py` drives an app over these endpoints
for the e2e tests.

[shell]: https://github.com/SanderMertens/flecs/blob/master/docs/FlecsShell.md
