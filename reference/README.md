# Reference sources for the OreElse port

Read-only copies from flecs-engine at commit `00911990`, for the tasks of the OreElse epic, which
port ore_else to box as `apps/oreelse`. Nothing here is built, linted or loaded by box.

- `ore_else/`: flecs-engine's `projects/ore_else` (C in `src/` and `include/`, Flecs scripts and
  SVG assets in `etc/`, the game's `README.md`). Its `project.json` and `CMakeLists.txt` are
  renamed to `*.orig` so bake does not build it.
- `flecs-engine/include/`: flecs-engine's headers, which define the components the scripts use
  (`include/modules/*.h`).

Port from these files; do not include, load or link them. Where the original's README and code
disagree, the code is the reference. The last task of the epic deletes this folder.
