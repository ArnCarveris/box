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
