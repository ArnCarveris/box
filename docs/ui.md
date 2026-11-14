# UI

Keyboard focus, Tab navigation, key capture, widget change/submit events, the theme and a
single-line text field (`TextEdit`). Module `box.ui` (`BoxUiImport`; imports types, transform,
input, picking). The widget templates are a separate script module, `box.widgets`
(`test/assets/ui/widgets.flecs`, app copy `etc/scenes/ui/widgets.flecs`), which `box.ui` does
not load. Layout comes from `box.layout`, text from `box.text`, pointer events from `box.picking`.

## Components

| Component (script name) | Key fields | What it does |
|---|---|---|
| `Focusable` (tag) | | Can take the keyboard focus; adds `Interactive`. Disabled entities/prefabs never take it |
| `Focused` (tag) | | On the one entity with the focus |
| `FocusScope` (tag) | | Tab / Shift+Tab cycle the `Focusable`s below it in 2D draw order |
| `KeyCapture` (tag) | | While focus is on it or a descendant, keys and text go to the UI only (bindings see keys up) |
| `FocusStatus` (singleton, read only) | `focused`, `previous`, `visible` | `visible` = focus came from the keyboard: show a focus ring |
| `UiChangeEvent` (`ChangeEvent`) | `widget` | What `on.change` / `on.submit` resolve with |
| `UiTheme` (`Theme`, on `box.ui.theme`) | colors `background surface surface_alt border text text_muted accent accent_text hover pressed focus danger selection caret backdrop`; `disabled_opacity` 0.45, `backdrop_opacity` 0.6, `font_size` 14, `small_font_size` 12, `heading_font_size` 18, `radius` 4, `padding` 8, `spacing` 8, `control_height` 28, `focus_width` 2, `caret_blink` 0.5 | Widget look; defaults = `box.widgets.DarkTheme` |
| `TextEdit` | `value`, `placeholder`, `max_length` (0 = none), `size` (16), `font`, `password`, `read_only` | Engine-edited single-line field. Adds `Focusable`, `KeyCapture`, `TextEditStatus`, `Cursor {CursorText}`, `Layout` |
| `TextEditStatus` (read only) | `caret`, `anchor` (bytes), `scroll`, `focused` | Caret/selection state |

## Script functions

| Function | Does |
|---|---|
| `await on.change(e)` / `await on.submit(e)` | Wait for a change/submit reported by `e` or any descendant; returns `ChangeEvent` |
| `await ui.changed(w)` / `await ui.submit(w)` | Report a change/submit for widget `w` |
| `await ui.delay(seconds)` | Wait in world time |
| `ui.activation(e)` | Entity an `on.activate` event counts for (0 for a click released elsewhere, other button, or Enter/Space on a `KeyCapture` widget) |
| `ui.sibling(e, offset, with)`, `ui.root(e)`, `ui.focused()`, `ui.first_focusable(scope, skip)` | Helpers used by the widget library |
| `focus(e)`, `on.focus`, `on.blur`, `on.key_down`, `on.key_up`, `on.activate` | From `flecs.script.input`; `box.ui` drives its focus |

## Widgets (`box.widgets`)

| Template | Props (defaults) | `mut current` |
|---|---|---|
| `Label` | `text`, `variant` (`body`/`muted`/`heading`/`caption`), `width` (0 = no wrap) | |
| `Panel` | `title`, `width` | (column container) |
| `Separator` | | |
| `Button` | `label`, `variant` (`primary`/`secondary`/`ghost`/`danger`), `action` (taps an Action), `disabled`, `width` | (reports a change if no `action`) |
| `Checkbox`, `Switch` | `label`, `value`, `disabled`, `width` | bool |
| `RadioGroup` > `RadioButton` | group: `value` (label of checked), `disabled`, `width`; button: `label`, `disabled` | string |
| `Slider` | `min` 0, `max` 100, `step` 1, `value`, `label`, `decimals`, `unit`, `thumb` 16, `disabled`, `width` | f32 |
| `Stepper` | `min` 0, `max` 10, `step` 1, `value`, `decimals`, `button` 28, `disabled`, `width` | f32 |
| `ProgressBar` | `value` (0..1), `label`, `width` | f32 |
| `TextInput` | `label`, `placeholder`, `value`, `password`, `read_only`, `disabled`, `width` | string |
| `Dropdown` | `options` (list), `value` (index), `row` 28, `inset` 4, `disabled`, `width` | i32 |
| `ScrollView` | `width`, `height`, `horizontal` false, `vertical` true, `bar` 10, `step` 40, `keys` true, `focusable` true | |
| `ListView` | `items`, `value` (-1), `row`, `height`, ... (a ScrollView) | i32 |
| `TabView` > `Tab` | view: `value`, `width`, `height`; tab: `label` | i32 |
| `Collapsible` | `title`, `value` (open), `disabled`, `width`, `header` 32 | bool |
| `Tooltip` (child of the widget) | `text`, `delay` 0.5, `width` 240 | |
| `Modal`, `CloseButton` | `open`, `title`, `width`, `dismiss` true | bool (open) |

Each styled template inherits a `Headless*` template (`Button : HeadlessButton`) that owns state
and input; restyle by inheriting the headless one in your own template.

## Flecs script

```flecs
include ui/widgets            // path relative to this file (etc/scenes/ui/widgets.flecs)

using flecs.script
using box.layout
using box.ui
using box.widgets

state { export mut volume: f32 = 40 }

root {
  Fill: {surface: window}     // window: an entity with a Surface
  Layout: {direction: Column, spacing: 12, padding: {16, 16, 16, 16}}
  FocusScope                  // Tab moves between the widgets below

  Panel settings(title: "Audio", width: 300) {
    Slider volume(label: "Volume", min: 0, max: 100, step: 10, value: 40, unit: "%")
    Checkbox mute(label: "Mute")
    Dropdown quality(options: ["Low", "High"], value: 1)
    Button save(label: "Save") { Tooltip tip(text: "Writes the settings") }
  }
}

async {
  while true {
    const e = await on.change(root.settings)
    const w = e.widget
    if w.has(Slider) { state.volume = w[Slider.mut].current }
  }
}

// Switch theme: restyles every widget next frame
box { ui { theme { Theme: LightTheme } } }
```

Raw text field: `field { TextEdit: {placeholder: "Name"}; Item: {width: {mode: Fixed, value: 200}} }`.

## C API

`box_ui_focus(world, e)` (0 clears; returns whether focus is on `e`), `box_ui_text(world)` /
`box_ui_paste(world)` (text `KeyCapture` took this frame), `box_ui_word_move(text, offset, fwd)`.
Load the widgets from C: `ecs_script(world, { .entity = ecs_entity(world, { .name =
"box.ui.widgets" }), .filename = box_app_locate("test/assets/ui/widgets.flecs") })`.

## Behavior and gotchas

- A press focuses the first `Focusable` on its bubbling path, or clears focus. Disabling,
  deleting or removing `Focusable` from the focused entity clears it (deleted gets no `on.blur`).
- `on.focus`, `on.blur`, `on.activate`, `on.change` reach ancestors too: check
  `e.target == this` (focus) or `ui.activation(e) == this` (activate) for your own events.
- While focus is keyboard-visible, Tab, Enter, Space, arrows, Home, End, PageUp/Down and Escape
  go to the UI only. A pointer focus leaves all keys to bindings too. The console toggle key
  always reaches the console.
- Tab skips entities fully clipped by `ClipChildren` (collapsed sections, hidden tabs) but not
  content a scrolling clip hides; a `ScrollView` scrolls the focused widget into view.
- With nothing focused, Tab focuses the first `Focusable` of the first `FocusScope` in the views
  of the focused surface. Without any `FocusScope`, Tab is a plain key.
- Widget values: `prop value` is the initial value, `mut current` the live value. Setting the mut
  from C/shell (`set settings.volume Slider.mut {current: 20}`) or changing `value` updates the
  widget without a change event.
- `TextEdit.value` is the source of truth; an edit writes it with `ecs_modified` and reports
  `on.change`. Newlines/tabs become spaces, control characters are dropped. Offsets are bytes.
- Text field keys: Ctrl (Alt on macOS) word moves, Ctrl/Cmd+A/C/X/V, Escape blurs, Enter
  submits; double click selects a word, triple click all.
- Floating popups (Dropdown list, Tooltip, Modal) use `layout.Float` and ignore clips.
- `Modal` blocks input under it, keeps focus and Tab inside while open; Escape, `CloseButton`
  or a backdrop click close it with a change.

## Run and test

```sh
bake3 run apps/boxrun --local-env -- etc/scenes/ui/gallery.flecs
bake3 run test/cpu/ui --local-env -- Widgets      # also Focus, TextEdit, UiEvents
bake3 run test/gpu/ui --local-env -- Focus        # also TextField, Picking
```
