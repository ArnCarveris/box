# Console

In game console: an overlay panel that runs command lines in a flecs shell session, with syntax
coloring, typed output, history, Tab completion and readline keys. Module `box.console`
(`BoxConsoleImport`, imports `box.command`). The commands it runs are those of
[command.md](command.md) plus the flecs shell builtins.

## Components and entities

| Name | Key fields / role |
|---|---|
| `Console` (singleton, `box.console.Console`) | `open`, `width` (0.5 of surface, from the left), `height` (1, from the top), `font_size` (16 pt), `line_height` (20), `padding` (10), `scrollback` (1000 rows), `background` (rgba), `border`, `text`, `command`, `error`, `input` (rgb), `syntax`, `output` |
| `Syntax` (`ConsoleSyntax`) | Input line colors: `name`, `unknown`, `entity`, `missing`, `number`, `string`, `boolean`, `value`, `extra`, `scope`, `symbol`, `ghost` |
| `Output` (`ConsoleOutput`) | Output colors by result type: `dim`, `entity`, `component`, `key`, `number`, `string`, `boolean`, `keyword`, `command`, `param`, `type`, `brief`, `group`, `match`, `added`, `removed` and the ANSI colors `red green yellow blue magenta cyan white grey` |
| `box.console.toggle` | `Action` bound to the grave key; enabled only while the world has a `Surface` |
| `console.session` | Shell session the lines run in (created on first open) |
| `console.ui`, `console.view` | Scene root and view of the content, drawn at `BOX_VIEW_ORDER_CONSOLE` (after every other view, over the fps panel and editor) |

A width or height fraction <= 0 is a panel of zero size, >= 1 fills the axis. The panel follows
surface resizes; lines wrap at the panel width minus padding.

## Script

```flecs
using box.console

$ {
  Console: {open: true, width: 0.8, font_size: 11, line_height: 13}
}

const out: string = box.console.run("ls items.lamp")
const typed: string = box.console.type("cat ")
const listed: string = box.console.complete(false)
```

| Function (script / C) | Does |
|---|---|
| `box.console.run(line)` / `box_console_run(world, line, &error)` | Runs a line as if typed and entered: adds it and its output to scrollback and history. Returns the console's text (laid out for reading; differs from `box_command_run`) |
| `box.console.type(text)` / `box_console_type(world, text)` | Types text at the cursor (control chars dropped), shows the history suggestion; returns the input line |
| `box.console.complete(back)` / `box_console_complete(world, back)` | Tab (`back` = Shift+Tab); returns the input line |

## Keys while open

| Key | Does |
|---|---|
| Enter | Run the line |
| Up / Down | Walk history (256 lines kept) |
| Right / End | Accept the history suggestion (ghost text) |
| Tab / Shift+Tab | Complete command names, `help` topics, entity arguments. One candidate inserts, several insert the common prefix or open a list (24 shown) that further Tabs cycle; no candidate flashes the prompt |
| Left / Right / Home / End, Backspace / Delete | Move the cursor, erase |
| Ctrl+A / E / U / K / W | readline: start, end, erase to start, erase to end, erase word before the cursor |
| Ctrl+C | Abandon the line (echoes `^C`) |
| Ctrl+L | Clear scrollback (same as `cls`) |
| Page Up / Down, wheel | Scroll (wheel: 3 rows per step) |
| Escape | Close the candidate list when open, else the console |
| Grave | Toggle (`box.console.toggle`) |

## Behavior

- While open the console takes the keyboard: typed text and key presses go to the input line and
  every other key/button is released, so no other action fires; only the toggle still sees keys.
- Prompt is the session scope (`~` at the root) followed by `$`; `cd` changes it and each line
  that ran keeps the prompt it had.
- `cls` is a console-only command (clears the scrollback); it is not available in other sessions.
- First open adds a hint line: `Flecs Shell  type help for a list of possible commands`.
- Coloring is console only: sessions, logs and `box_command_run` output stay plain.
  - Input: first token is the command name, further tokens take the color of their parameter
    type; entities resolve from the session scope (`missing` when they do not).
  - Output: laid out per result type like the flecs explorer terminal (help, tree, query rows,
    entity info, docs, grep matches, scripts, diffs, values). Errors start with `error:` in
    `Console.error`, further error lines dimmed. ANSI color codes map to the `Output` ANSI colors.
- Content, view and session are created on first open, not at import.

## Gotchas

- Without a `Surface` the toggle action is disabled, so the grave key does nothing; set
  `Console.open` from a script, C or REST instead.
- Test scripts drive it with `box.console.run/type/complete` (see
  `test/integration/visual/ui_console_*.flecs`), not with key input.
