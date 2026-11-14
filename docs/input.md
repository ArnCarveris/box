# Input

Keyboard and mouse device state, named actions with key bindings, a synthetic input queue for
agents/tests, clipboard and the platform backends (glfw, browser). Module `box.input`
(`BoxInputImport`; imports `box.types`, `flecs.script.input`). Device events also go out as
`flecs.script.input` events (`on.key_down`, ... ), aimed at entities by `box.picking`.

## Components

| Component (script name) | Key fields | What it does |
|---|---|---|
| `Input` (singleton, written) | `keys[104]` (`KeyState` by `Key`), `mouse`, `text`, `paste`, `size`, `focused`, `surface`, `capture` | Device state of this frame. Set `capture` to capture the cursor (mouse look) |
| `KeyState` | `down`, `pressed`, `released`, `repeated` | One key/button this frame |
| `Mouse` | `position` (points, top-left origin), `delta`, `normalized` (0..1), `scroll`, `pinned`, `captured` | Cursor and wheel |
| `Action` | `description` | A named game action entity. Implies `ActionState` |
| `ActionState` (written) | `down`, `pressed`, `released`, `start`, `delta`, `drag` | Resolved every frame |
| `Binding` | `action` (0 = self if an Action, else parent), `key`, `ctrl`, `shift`, `alt`, `meta`, `ignore_modifiers` | Key (or mouse button) + modifiers to action |
| `ActionEvent` | `action`, `screen_x/y`, `delta_x/y`, `drag_x/y` | What `on.pressed/held/dragged/released` resolve with |
| `InputEvent` (`Event`) | `kind` (`EventKind`), `key`, `code`, `value`, `normalized`, `repeat`, `delay`, `action`, `surface`, `text` | One device or synthetic event |
| `Key` enum | `A`..`Z`, `Num0`..`Num9`, `Space Enter Escape Tab Backspace Insert Delete Home End PageUp PageDown Left Right Up Down`, `F1`..`F12`, `ShiftLeft/Right CtrlLeft/Right AltLeft/Right SuperLeft/Right CapsLock`, punctuation, `Kp0`..`Kp9 KpEnter ...`, `MouseLeft MouseMiddle MouseRight` | Script drops the `Key` prefix |
| `PointerCursor` enum | `CursorDefault CursorPointer CursorText CursorCrosshair CursorResizeH CursorResizeV CursorMove CursorNotAllowed` | Shape set via `box.picking`'s `Cursor` |

## Flecs script

```flecs
using flecs.script
using box.input

actions {
  jump {
    Action: {description: "Jump"}
    Binding: {key: Space}
    pad { Binding: {key: W} }            // child binding: action 0 -> parent
  }
  save {
    Action: {description: "Save"}
    Binding: {key: S, ctrl: true}        // Ctrl+S masks a plain S binding
  }
}

async {
  while true {
    const e = await on.pressed(actions.jump)   // also on.held, on.dragged, on.released
    // e.screen_x, e.drag_x, ...
  }
}
```

A widget/element can drive an action with `box.picking`'s
`ActionTrigger: {action: actions.jump, mode: ActionTriggerHold}` (modes: Click, Press, Hold).

## Shell commands (console, REST shell)

```
key_press W [frames]        key_down W / key_up W     key_repeat W [count] [frames]
key_type hello [frames]
mouse_move [600, 60] [norm]  mouse_click left [670, 360] [frames]   mouse_button left true
mouse_sweep / mouse_drag left ... (from [x, y] to [x, y], steps, frames)
mouse_release                mouse_scroll 0 -1
action_press actions.jump [frames]   action_down / action_up   action_drag   action_list
```

Key names are case-insensitive: `Key` constants (`W`, `Space`, `MouseLeft`), DOM key values
(`ArrowLeft`, `Control`) or single characters (`a`, `/`). Commands return the frames until the
queue drains.

## C API

| Group | Functions |
|---|---|
| Synthetic queue | `box_input_key`, `box_input_key_press`, `box_input_key_repeat`, `box_input_key_type`, `box_input_mouse_move`, `box_input_mouse_sweep`, `box_input_mouse_click`, `box_input_mouse_drag`, `box_input_mouse_scroll`, `box_input_mouse_release`, `box_input_action`, `box_input_action_press`, `box_input_action_drag`, `box_input_paste`, `box_input_focus`, `box_input_reset`, `box_input_pending`, `box_input_queue` |
| Clipboard | `box_input_clipboard_set`, `box_input_clipboard_get` |
| Platform | `box_input_platform_set` (`box_input_platform_t`: poll, free, capture, clipboard_set/get, cursor), `box_input_platform_push`, glfw/dom translators (`box_input_glfw_key`, `box_input_dom_on_key`, ...) |
| Names | `box_input_key_from_name`, `box_input_key_name`, `box_input_cursor_get` |

```c
box_input_key_press(world, KeyW, 3);                 // hold W for 3 frames
box_input_mouse_click(world, KeyMouseLeft, (float[]){640, 360}, false, 1);
while (box_input_pending(world)) ecs_progress(world, 0);
```

## Systems

`box.input.Update` then `box.input.ActionUpdate`, both `EcsOnLoad`: apply platform + synthetic
events to `Input`, emit script events, resolve bindings into `ActionState`, raise `on.*`.

## Behavior and gotchas

- Key codes are physical positions on a US layout: `W` is the key left of `E` on every layout.
  Typed characters arrive separately in `Input.text`.
- Binding modifiers: of the bindings whose modifiers are held when the key goes down, the most
  specific win (Ctrl+S masks S), unless an extra modifier is held that no binding uses and the
  binding does not `ignore_modifiers`. The action stays down until the key comes up or a required
  modifier is released. Disabled entities and prefabs do not bind.
- An action is down while any `Binding`, `ActionTrigger` or synthetic hold keeps it down.
- The synthetic queue applies one event after another; `delay` frames wait after the previous.
  A synthetic move pins the cursor (platform moves ignored) until `mouse_release`.
- Losing focus (`box_input_focus(world, false)` or a platform focus event) releases held keys and actions and
  ignores platform input. Only events of `Input.surface` apply (any surface while 0).
- `box.ui`'s `KeyCapture` and a keyboard-visible focus hide keys from bindings (`Input.keys` sees
  them up, `Input.text` empty). An `InputBlocker` under the cursor keeps presses and the wheel
  from the actions.
- Capture: native uses glfw's disabled cursor with raw motion; the browser uses Pointer Lock,
  granted at the next click or key. `mouse.captured` says when the platform holds it.
- Clipboard: glfw pastes on Ctrl+V / Ctrl+Shift+V / Cmd+V into `Input.paste`; the browser pastes
  through its own paste event and may refuse writes without a user activation. Headless runs
  use an in-memory clipboard.

## Tests

```sh
bake3 run test/cpu/input --local-env -- Action   # also Clipboard, Device, Platform, PointerShape
```
