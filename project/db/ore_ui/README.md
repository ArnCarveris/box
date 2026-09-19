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

Do not transliterate the flecs-engine widgets. For every screen, first redesign it on what `box.ui`
already has (`Layout` rows and columns with padding, spacing and alignment, `Anchor`, `Bounds`,
`Text` and the text helpers, `Image`, `Border`, `BoxShadow`, `Opacity` groups, hover, press, click
and drag events with bubbling, `Action`, `TextInput`, the console as the reference widget) and
only then list what the screen still cannot express. Those leftovers are implemented in `box.ui`
as canonical features that anyone who has built user interfaces would recognise, not as game
specific components: for example a button with hover, pressed and disabled states and a style, a
progress bar, a scroll view with a scrollbar, a grid layout, a tooltip, a modal that takes focus,
a slider, a tab strip, a tree or list with selection, margins next to padding, a style sheet of
named colors and sizes. The `ore_engine/ui_canonical` task holds that list; the widgets task fills
it before any screen is built and the screens consume the results.
