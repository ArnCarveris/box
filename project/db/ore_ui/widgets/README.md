# Widget library

The game's widget templates rebuilt on the box UI components.

## Details

First the redesign: for each of the game's widget templates and screens, write down how it is
built from the existing `box.ui` components and which canonical script widgets it needs; that list
is what `ore_engine/ui_canonical` ships, as flecs script templates and not as C, before the screens
are assembled. The game's widgets themselves are then thin templates over the library, in the
game's `etc/ui/widgets/`, and stay pure script.

Port ui/widgets/*.flecs: Panel, ActionButton, SlotButton, StockSlot, PowerSegment, QueueRow,
RocketStep and the rest as templates over Layout, Anchor, Text, Image, BoxShadow, Opacity groups and
events, with the icons as Image quads from the SVG atlas.

Verification: Unit tests per widget through the layout pass, a visual gallery scene.

## References

- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/ui/widgets
- project/db/ui/deliverable/README.md
