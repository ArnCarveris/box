# Canonical UI widgets in script

The widget library the Ore Else screens need, written in flecs script on top of box.ui, with the engine gaining only the primitives a script cannot express.

## Details

Widgets are flecs script templates, not C. The reference is the flecs repository's script examples (`/Users/sandermertens/GitHub/SanderMertens/flecs/examples/script/12_slider.flecs`, `13_checkbox.flecs`, `14_dropdown.flecs`, `15_radio_group.flecs`, `16_mixer.flecs`, `17_lights_out.flecs`, with `11_template_interface.flecs` and `12_template_inheritance.flecs` for props and inheritance) and the console in `etc/ui/console.flecs`: a widget is a template with props for its size, label and style, builds its look from `Layout`, `Anchor`, `Border`, `Rgb`, `Image`, `Text` and `BoxShadow`, and drives its behaviour with async blocks awaiting `on.hover`, `on.press`, `on.release`, `on.click` and `on.drag`, with `with`, `if`, `for` and `match` for its variants, and reads and writes ordinary components so the game and the tests see state as data. Ship the library under the framework `etc/ui/widgets/*.flecs` (installed with the framework etc, loaded with `using box.ui.widgets` or an explicit load) with the canonical set the redesign in `ore_ui/widgets` asks for: a button with hover, pressed and disabled looks and a style, a progress bar, a scroll view, a grid, a tooltip, a modal, a slider, a checkbox, a radio group, a dropdown, a tab strip, a list with selection, and a style sheet as a prefab of named colors and sizes. The engine changes only where script cannot reach: a clip rectangle and a scroll offset on a node for the scroll view, keyboard focus and a wheel event through the existing input events, a hover delay timer if `on.hover` cannot express one, and text helpers (`TextInput`, `ConsoleLog`, measuring and wrapping) which stay the only native widgets. Anything a widget needs that is missing from script itself goes to the flecs repository as a script feature, not into C here.

Verification: every widget has a script test in `test/ui` that instantiates it, drives it through the input and picking commands and asserts its components; a gallery scene per widget family baselined; no new widget code in `src/ui` beyond the primitives above, checked in review; every existing scene unchanged.

## References

- `/Users/sandermertens/GitHub/SanderMertens/flecs/examples/script`, `/Users/sandermertens/GitHub/SanderMertens/flecs/docs/FlecsScript.md` (templates, async, match, with)
- `etc/ui/console.flecs`, `project/db/ui/deliverable/README.md`, `project/db/ore_ui/README.md`
