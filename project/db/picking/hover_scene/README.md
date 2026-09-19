# Hover highlight scene

A visual scene where the cursor rests on an object, the on.hover handler tints it and the baseline shows the highlight.

## Details

The picking task shipped the events with a command test only. This scene places a row of prefab objects, moves the cursor over the middle one with the input commands of the harness (or a `CommandScript` on the first frame) and lets a template `on.hover` handler set a tint while `Hovered` is present and clear it when it leaves, so the baseline shows one highlighted object and proves the ray, the root resolution and the script event end to end in a picture.

Verification: `picking_hover.flecs` baselined in `test/integration/visual`, every other scene unchanged.

## References

- `project/db/picking/picking/deliverable/README.md`, `test/integration/command/picking*.cmd`
