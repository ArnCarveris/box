# Strict layer views

A View flag that excludes entities without any layer when the view lists layers, so the render command isolates a live entity.

## Details

Entities without a layer pair are visible in every view so that a scene with one restricted view still shows its floor and props. The render command needs the opposite for a live subject: only the members of its temporary layer. `View.strict_layers` (default false) makes a view with a non empty layer list draw members only, applied to static runs, dynamic instances, transparent entries and lights alike, and the render command sets it on its temporary view.

Verification: unit tests in test/view for the flag with static, dynamic and light paths, the render command test for a live entity asserting the packet contains only the subject's instances, and the command integration scene's entity image showing the crate alone.

## References

- `project/db/views/render_command/deliverable/README.md`
- `src/render/batch.c`, `src/render/extract.c`
