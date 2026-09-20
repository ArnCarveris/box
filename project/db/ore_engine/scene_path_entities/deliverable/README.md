# Scenes loaded by absolute path create an empty root

## Built
`src/core/args.c` loads every scene named on the command line on an explicit entity,
`scenes.<stem of the file>`, so no root entity is derived from the path and a reload of the same
file lands on the same entity. The other `ecs_script` calls in `src` (`ui/console.c`,
`ui/widgets.c`) already name their entity.

## Verified
`test/core` Args `a_scene_loads_under_the_scenes_scope`: a scene loaded by absolute path exists
as `scenes.<stem>` with `EcsScript`, its content is at the root, and no root entity has an empty
name. Args suite 20 green. `bench: false`.
