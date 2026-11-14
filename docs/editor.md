# Editor

In-process editor: a panel docked to the left or right side of the surface, whose UI is a
flecs script (`etc/editor/editor.flecs`, template `box.editor.EditorPanel`, currently an entity
tree). Module `box.editor` (`BoxEditorImport`; imports transform, geometry, camera, surface,
view, material, layout, text, input, picking, ui).

## Editor singleton

| Field | Default | Meaning |
|---|---|---|
| `open` | false | Shown. The `edit` command flips it |
| `width` | 0.25 | Panel width as a share of the surface width, clamped to [0, 1] |
| `side` | `EditorSideRight` | `Right` or `Left` in script (`Left` sits under the console) |

Changes apply in the next frame (`box.editor.Update`, `EcsOnLoad`). The panel is full surface
height and follows surface resizes.

## Open / close

```flecs
using box.editor

$ {
  Editor: {open: true, width: 0.3, side: Left}
}
```

```sh
edit          # toggle (in-game console, REST shell)
edit on
edit off
```

```c
Editor *e = ecs_singleton_get_mut(world, Editor);
e->open = true;
ecs_singleton_modified(world, Editor);
```

## Entities and paths (`include/box/editor.h`)

| Macro | Value |
|---|---|
| `BOX_EDITOR_SCOPE` | `editor` (root of content and view, not under the module) |
| `BOX_EDITOR_UI` | `editor.ui` (a `Scene` with an ortho camera, one point per unit) |
| `BOX_EDITOR_VIEW` | `editor.view` (exists only while open) |
| `BOX_EDITOR_SCREEN` | `editor.ui.screen` (layout root, `LayoutFill` of the surface) |
| `BOX_EDITOR_PANEL` | `editor.ui.screen.panel` (instance of the template, `InputBlocker`) |
| `BOX_EDITOR_SCRIPT_PATH` | `etc/editor/editor.flecs` (found in cwd or an ancestor) |
| `BOX_EDITOR_SCRIPT` | `box.editor.script` (managed script entity) |
| `BOX_EDITOR_TEMPLATE` | `box.editor.EditorPanel` |

The view renders at `BOX_VIEW_ORDER_EDITOR`: over the game views, under the console; no clear,
lighting, culling or effects.

## Behavior and gotchas

- The content, view and script are created lazily on first open. Closing deletes only the view;
  content stays, so reopening is fast.
- `etc/editor/editor.flecs` uses `box.widgets` (inherits `HeadlessButton`, uses `Label`,
  `ScrollView`, ...). The widget library must already be loaded in the world, e.g. a scene that
  `include`s the widgets, or `ecs_script(world, { .entity = ecs_entity(world, { .name =
  "box.ui.widgets" }), .filename = "<path>/widgets.flecs" })` as the tests do.
- The script hot-reloads when the file changes (FileWatch). A failed reload keeps the entities of
  the last good load. A missing file logs a warning, leaves the panel empty and is looked for
  again on the next open.
- Pointer over the panel goes to the editor, not the game (`InputBlocker`). It takes the keyboard
  only while one of its widgets has focus; closing clears a focus inside it.
- Without a `Surface` in the world the content is laid out for 1280x720 points.

## Tests

```sh
bake3 run test/cpu/ui --local-env -- Editor
```
