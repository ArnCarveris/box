# UI

A retained UI framework rendered as an orthographic view, with layout, text helpers, group transparency and the in game console.

## Details

An entity with a Ui component roots a UI tree that is rendered through a view with an orthographic camera in logical pixels, on top of the 3D views. Inside a tree a child is always drawn over its parent, so there is no z fighting, and elements reuse Position, Rotation, Scale and the 2D shapes rather than anything UI specific. Layout, anchoring and a read only Bounds rectangle come from a layout pass, transparency on a parent composites the subtree as one image so children never ghost through each other, and BoxShadow uses the same analytic rounded rectangle as Border.

All widget behaviour lives in flecs script: the module has no widget specific logic, only the text helpers the brief asks for and the script events for hover, click and drag, which also emit input actions so a button and a key binding share one action. The in game console opened with the backtick key is built from these pieces and is the proof that the framework is enough for a real widget. Verification is unit tests for layout and hit testing, visual galleries, a scripted button driven by command based input tests and a console session capture. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: Ui, Layout, Anchor, Bounds, Text, BoxShadow and the console components, following design/datamodel/ui.flecs.

## References

- design/DESIGN.md, sections UI and Command interface (backtick console)
- design/datamodel/ui.flecs and design/datamodel/geometry.flecs (2D shapes)
