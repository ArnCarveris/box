# UI: completion report

## What was built
- `box.ui`: a `Ui` component roots a tree drawn by its own orthographic view in logical pixels, composited over the 3D views. The update runs immediate in PreUpdate next to the script update, walks the tree pre-order and gives every node a `UiNode` place, the layer of its tree and a depth bias, so a child is always over its parent without authored z; a UI view drops sun, lights and sky, hides its layers from other views and composites alpha plainly instead of through the glass model.
- `Layout` (row or column, padding, spacing, size, alignment per axis, size from content), `Anchor` (edge or centre pinning, out of the flow) and the engine written `Bounds`. The pass measures backward and places forward, runs only for marked subtrees (the nearest ancestor the change cannot resize) and numbers depth only when structure changed.
- `Font {path, size}` and `Text {value, line_height, align}`: stb_truetype rasterizes one atlas per font and size that doubles on demand, every glyph is an ordinary child quad on the 2D shape path, and `text.measure`, `text.wrap` and `text.label` build widgets from script. `etc/fonts/B612-Regular.ttf` (OFL) ships with the framework etc.
- `BoxShadow {x, y, blur, spread, color}` through the analytic rounded rectangle of `Border`, and group transparency: an `Opacity` below one renders the subtree into a `RenderTarget` the size of its `Bounds` through a view of its own and composites it once, so overlapping children do not ghost.
- UI events: a UI view answers its own pick through `Bounds` and world transforms (deepest node of the topmost subtree wins and consumes the pick before the scene ray), raises the same `on.hover/press/release/click/drag` with bubbling and consumption, and `Action` fires an input action on click.
- The backtick console: `TextInput` (editable line, caret, history, tab completion through `box_command_complete`) and a `Console` singleton that installs the action binding, loads the framework script `etc/ui/console.flecs` on first open, holds the keyboard while open, runs submitted lines on a terminal session and shows its log as scrollback. The overlay is built entirely in script, so an application ships its own by replacing that file; the app enables it in its default scene.

## Verification
- TESTS_PLACEHOLDER
- Scenes in `integration/`: `ui_panels` and `ui_panels_small` (nested panels at two surface sizes), `ui_layout` (the layout gallery), `ui_text` (paragraphs at four sizes and three alignments and a labelled button), `ui_shadow`, `ui_group_alpha` against `ui_group_alpha_direct` (the direct path ghosts, the texture path does not), `ui_hover` (the hovered button tinted), `console`; command scenes `ui_text_script` (a button built from helpers only), `ui_events` (a click bubbling from label to button and its action) and `console`; every earlier scene bit identical.
- Benchmarks in `bench/` against the checkpoints epic: a dynamic 10k node tree extracts in about 300 µs, a static one with one node moved in 20 µs (the tree part 0.5 µs), 10k elements lay out in 0.5 ms, one moved node relays out in 0.96 µs, 1000 changed glyphs in 1.2 ms, ten transparent panels extract in 41 µs against 27 µs opaque, and hit testing a 10k node tree costs 83 µs.
- Build reports and the wasm image in the deliverable, open it through the status page Inspect link of this epic.

## Lessons
- The UI update must stay immediate in PreUpdate beside the script update; a sync point anywhere else changes every scene.
- Layout before numbering: a node that gains its rectangle in the layout pass gains its instance there, and a depth bias set before the instance exists is lost.
- A node without a `Position` keeps a zero world matrix and collapses its subtree, so the layout pass writes the position of every node it places.
- `AlphaBlend` is a glass model in this engine; UI views needed a plain source over flag to composite glyphs and images cleanly.
- Marking whole trees for layout turned one moved node into a 600 µs frame; subtree marks brought it under a microsecond.

## Not in this epic
- The hit test walks every node of a view; a front to back walk with an early out is a follow up. A node created after its frame's update joins its layer one frame later.
- An opaque border has a hard edge where a blended one antialiases, and the shadow pass still casts the full rectangle of a rounded shape (2D shapes epic).
