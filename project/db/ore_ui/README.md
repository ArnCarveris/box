# Ore Else user interface

The widget library, HUD, inventory and craft screen, queue, rocket panel, title, pause and end screens and the key bindings, built from the box UI.

## Details

The game's twenty widget templates (panels, action and slot buttons, stock slots, power segments,
queue rows, rocket steps) are rewritten on the box UI components (Layout, Anchor, Text, Image,
BoxShadow, Opacity groups, events and actions), then the HUD, the inventory and craft screen, the
order queue, the rocket panel and the splash screens are assembled from them, with the key binding
help generated from the input maps.

Verification: Unit tests per widget through the layout pass, visual scenes per screen at two surface sizes, and
command based tests driving the inventory and queue through the mouse and keyboard commands.

## References

- design/DESIGN.md
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/README.md
- project/db/ore_engine/README.md

## Design rule

Do not transliterate the flecs-engine widgets, and write no widget in C. Every screen is first
redesigned on what `box.ui` already has (`Layout`, `Anchor`, `Bounds`, `Text` and the text helpers,
`Image`, `Border`, `BoxShadow`, `Opacity` groups, the hover, press, click and drag events, `Action`,
`TextInput`, the console as the reference) and then on the script widget library of
`ore_engine/ui_canonical`, which is flecs script templates in the style of the flecs repository's
own examples (`/Users/sandermertens/GitHub/SanderMertens/flecs/examples/script/12_slider.flecs` to `17_lights_out.flecs`). Behaviour lives in async blocks
awaiting the events, state lives in components, looks come from props and a style sheet prefab.
The engine gains only primitives a script cannot express (clipping, scroll offset, focus, wheel);
the only native widgets are the text helpers. Screens are data: a HUD or an inventory is a script
file the checkpoint can write and an agent can read, which is the point of the showcase.

## Porting rule

Ore Else was built on an older, inferior engine, and this port is the showcase for how box does it
better. Do not carry the old engine's structure, workarounds or C where box has a declarative way:
prefer script templates, components and the command interface over C systems, keep gameplay logic
in C only where it is genuinely simulation (grids, economy, AI), and where the old game worked around
a missing feature, file the feature as a box task instead of porting the workaround. Every screen,
effect and building should read as an example of the box way.
