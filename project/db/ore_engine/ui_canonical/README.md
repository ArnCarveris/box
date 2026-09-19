# Canonical UI features

The generic widgets and layout features the Ore Else screens need beyond the current box.ui, implemented as canonical UI concepts anyone who has built interfaces would recognise.

## Details

`ore_ui/widgets` starts by redesigning every game screen on the existing `box.ui` components and writes down what they cannot express. This task implements that list in `box.ui`, as generic features with the names and behaviour a web or desktop UI developer expects and none of the game's vocabulary: a `Button` with hover, pressed, disabled and focused states driven by the existing events and a `Style` of colors, borders and radii per state; a `ProgressBar` with a fill and a label; a `ScrollView` clipping its content with a scrollbar, wheel and drag; a `Grid` layout with columns, rows and spans next to the row and column `Layout`; a `Tooltip` shown after a hover delay; a `Modal` that takes focus and blocks events below it; a `Slider`; a `TabStrip`; a `List` with selection; `Margin` next to padding; a `StyleSheet` of named colors and sizes that templates reference. Only what the redesign asks for is built, and each feature comes with the same unit tests, visual gallery scene and README paragraph as the UI epic's own features.

Verification: unit tests per feature through the layout pass and the events, a gallery scene per feature baselined, every existing scene unchanged, and the widgets task consuming the features with no game specific code in `box.ui`.

## References

- `project/db/ui/deliverable/README.md`, `project/db/ore_ui/README.md`, `etc/ui/console.flecs`
