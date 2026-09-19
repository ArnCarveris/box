# Framework etc folder and shader include path

Built: the 23 engine shaders and their 19 includes moved from `apps/box_app/etc/shaders`
to `etc/shaders` of the framework, which bake3 installs as `<BAKE_HOME>/etc/box`. The
search path is the path as written (the working directory), `<BAKE_HOME>/etc/box` for a
path under `etc/` (`BAKE_HOME`, else `~/bake3`), the nearest ancestor of the working
directory with an `etc/shaders`, `apps/box_app` at or above it, and the exe folder. A file keeps the path it was asked for, so its includes go through the search
path again and an app can override an engine include. The wasm image embeds both `etc`
folders.

Verified: 30 `shader` tests (6 new in the `Path` suite: the bake environment, the
`~/bake3` fallback, the source tree without an install, the working directory before the
environment, an app override of an engine include and the engine include without one),
233 `render`, 140 `view` and the other nine test projects green, `tools/lint.py` clean,
all 43 visual scenes bit identical, and the wasm build links with the 42 shader files
embedded at `/etc/shaders`, which the working directory entry finds.

Left out: no bench, the move changes no runtime path (`"bench": false`). The app has no
`etc/shaders` of its own left; one it adds is searched before the installed copy.
