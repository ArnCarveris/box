# Task

`box.task` (`src/task`, `include/box/task.h`) collects the background work modules report
(shader compiles, font and texture loads, probe bakes, ...) into the `InProgressTask` singleton,
which the startup overlay ([startup.md](startup.md)) and scripts read.

## Components

| Component | Fields | What it does |
|-----------|--------|--------------|
| `InProgressTask` | `count`, `blocking`, `tasks[16]` | Singleton aggregate, read only. Blocking tasks first, each group in start order; entries from `count` on are zero |
| `InProgressTaskItem` | `name`, `blocking`, `done`, `total` | One task: all reports of the same `name` combined. `blocking` if any report blocks; `total` 0 when unknown |

At most `BOX_TASK_MAX` (16) entries; more tasks are dropped. The singleton is only written when
it changes.

**Blocking** = the scene can't be shown as intended until it's done (a pipeline the first frame
draws with, a texture the scene uses). **Not blocking** = refines what's already shown.

## Built-in task names

| Macro | Name | Blocking | Reported by |
|-------|------|----------|-------------|
| `BOX_TASK_SHADERS` | Compiling shaders | yes | render/shader.c |
| `BOX_TASK_FONTS` | Loading fonts | yes | text |
| `BOX_TASK_TEXTURES` | Loading textures | yes | texture |
| `BOX_TASK_TEXTURE_GEN` | Generating textures | yes | texture (TextureGen) |
| `BOX_TASK_ENVIRONMENT` | Loading environment | yes | render/environment_image.c |
| `BOX_TASK_PROBES` | Baking light probes | no | render/bake.c |
| `BOX_TASK_REFLECTIONS` | Capturing reflections | no | render/reflect.c |
| `BOX_TASK_SOUNDS` | Decoding sounds | no | audio/sample.c |

## Reading in a script

```flecs
using box.task

const tasks = InProgressTask[InProgressTask]
// tasks.count, tasks.blocking, tasks.tasks[i].name / .done / .total / .blocking
```

## C API

All report functions take the world or a stage, may be called from any thread (registry behind
a lock) and do nothing without box.task / with a 0 handle.

| Function | Use |
|----------|-----|
| `box_task_id_t box_task_start(world, name, blocking, total)` | Start a report (name copied; `total` 0 = unknown). Returns 0 without box.task |
| `box_task_advance(world, id, done)` | Add finished units |
| `box_task_set(world, id, done, total)` | Set progress |
| `box_task_end(world, id)` | Finished or dropped; ending twice is a no-op |
| `box_task_remaining(world, &track, name, blocking, remaining)` | Once per frame from a count of remaining work: starts/ends the report itself |
| `box_task_find(tasks, name)` | Entry of `InProgressTask` by name, NULL if none |
| `box_task_publish(world)` | Write `InProgressTask` now (main thread, outside a frame or in an immediate system) |

Typical per-frame reporter (as texture/text/audio do):

```c
static box_task_track_t track; /* zero before the first call; usually module state */
box_task_remaining(world, &track, BOX_TASK_TEXTURES, true, loads_in_flight);
```

`box_task_remaining` grows `total` by every increase of `remaining`; `done = total - remaining`.

## Timing

- `InProgressTask` is written by `box.task.PublishTasks` in the `TaskPublish` phase
  (`box.task.Publish`), which depends on `EcsPostFrame`: the last of the frame.
- A module that reports in a later phase must make `TaskPublish` depend on it; box.render does
  `ecs_add_pair(world, TaskPublish, EcsDependsOn, RenderSubmit)`.
- Values seen by systems during a frame are those of the previous frame's publish.
