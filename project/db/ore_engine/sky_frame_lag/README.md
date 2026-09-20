# Time of day and weather reach the frame a frame late

The time of day and weather systems set the sun rotation and sky state through the stage in OnUpdate with no merge before extract, so a frame renders the sky of the previous frame; a sync point any immediate system adds later in the frame shifts eight sky baselines.

## Details

`src/sky/time.c` and `src/sky/weather.c` run in OnUpdate and write the sun's `Rotation` and the
`SkyState` with `ecs_set` on the stage, which flecs flushes at the next merge. `BoxRenderExtract`
in PreStore declares no terms, so no merge is inserted before it and the writes reach the world
at the end of the frame: the transform of the sun and the sky the packet carries are one frame
old. Adding the world anchor layout system (`box.ui`, OnValidate, immediate) put a merge before
extract and moved `sky_fog`, `time_of_day_*` and `weather_*` by up to 144 in a channel over
thousands of pixels, all of them one frame of progression; those baselines were refreshed with
that reason. Make the sky reach its own frame without depending on a merge another module
happens to add: declare the writes in the queries of the systems, or run the systems immediate,
and state in the deliverable which merge points the frame has after the change.

Verification: a `test/sky` case that the sun's WorldTransform after one progress matches the
rotation the time of that frame gives, the sky scenes still matching their refreshed baselines
when the world anchor systems are removed from the pipeline in a test world.

## References

- `src/sky/time.c`, `src/sky/weather.c`, `src/render/extract.c`, `src/ui/world.c`,
  `project/db/ore_engine/world_anchor/deliverable/README.md`
