# Canonical UI widgets in script

## Built
`etc/ui/widgets/widgets.flecs`, loaded by `box.ui` at import into the module `box.ui.widgets`
(`using box.ui.widgets`): a `Style` struct with a `DefaultStyle` prefab of colors and sizes
every widget takes as `style`, and the templates `Button` (hover, pressed and disabled looks,
fires `action`), `ProgressBar`, `Slider` (press and drag), `Checkbox`, `RadioGroup` with
`RadioButton`, `Dropdown` with `DropdownItem`, `TabStrip` with `Tab`, `List` with `ListRow`
(scrolls), `Grid` with `GridCell`, `Tooltip`, `Modal` and `ScrollView`. State is the prop and
mut variables of an instance, which a game or a test reads and writes as data; behaviour is
async blocks awaiting `on.press`, `on.release`, `on.click` and `on.drag`; looks read
`this?[Hovered]`. No widget code in C.

Engine primitives the scripts could not express: `HitThrough`, a tag on a caption or an
indicator so the widget behind it is what the cursor is on; scripts loaded under a module
stay out of checkpoints (`box_checkpoint_in_module`), so the library is neither written nor
reported as unowned. And a layout bug the galleries exposed: a Rect the layout wrote for a
content sized element was mistaken for an authored box, so a panel never grew to widget
children that got their size a frame later; `UiContentRect` marks such rects and the layout
measures the content again (`Layout.content_sized_ancestors_follow_a_grandchild_that_appears_later`).

## Verified
`test/ui` Widgets, 17 cases, one per widget driven through injected cursor moves, clicks and
drags, plus the style sheet and two layout cases; Hit gained the HitThrough case; `test/ui`
243 green, `test/checkpoint` 41 green with the module case. Gallery scenes baselined:
`widgets_buttons` (buttons in every look with a terminal hovering one, checkbox, radio
group, tabs, tooltip), `widgets_inputs` (progress, slider, open dropdown, list) and
`widgets_panels` (grid, scroll view, open modal with content). Every other scene unchanged.

## Left out
No hover delay on the tooltip and no keyboard focus beyond `TextInput`; a Dropdown, TabStrip
and List resolve the clicked choice from the point and their geometry, so a custom row size
must match the props. `bench: false`, nothing per frame changed.
