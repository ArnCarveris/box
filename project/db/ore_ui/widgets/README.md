# Widget library

The game's widget templates rebuilt on the box UI components.

## Details

First the redesign: for each of the game's widget templates and screens, write down how it is
built from the existing `box.ui` components and where that falls short; the shortfalls become the
list of canonical features in `ore_engine/ui_canonical` (a button with states and a style, a
progress bar, a scroll view, a grid layout, a tooltip, a modal, a slider, a tab strip, a list with
selection, margins, a style sheet), each described the way a web or desktop UI developer would
expect it to behave, and that task ships them in `box.ui` before the screens are assembled.

Port ui/widgets/*.flecs: Panel, ActionButton, SlotButton, StockSlot, PowerSegment, QueueRow,
RocketStep and the rest as templates over Layout, Anchor, Text, Image, BoxShadow, Opacity groups and
events, with the icons as Image quads from the SVG atlas.

Verification: Unit tests per widget through the layout pass, a visual gallery scene.

## References

- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/ui/widgets
- project/db/ui/deliverable/README.md
