# Crash at fini when box.ui imports after box.controller

A world that imports BoxController and then BoxUi frees a render query twice in box_render_state_dtor at ecs_fini; module import order must not matter.

## Details

The one shot effects task found the crash and worked around it by importing `BoxUi` first. Write the reproducer as a `test/render` or `test/ui` case that imports the two modules in that order and finishes the world, find which query `box_render_state_dtor` frees that another module's dtor or the UI view binding already released (or which registration runs twice when the render state is created by a dependent module before `BoxRender` itself), fix it in `src/render`, and add a case that imports every box module in reverse dependency order and finishes cleanly.

Verification: the reproducer fails before the fix and passes after, every test project green.

## References

- `project/db/ore_engine/one_shot_effects/deliverable/README.md`, `src/render/render.c`, `src/ui/view.c`
