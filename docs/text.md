# Text

Fonts loaded on the job system and strings drawn as one quad per glyph from a multi-channel
signed distance field (MSDF) atlas, crisp at any scale. Every `Text` is measured into
`TextBounds` for `box.layout`. Module `box.text` (`BoxTextImport`; imports task, layout, app,
jobs). Script names drop the `Text` prefix: `Text`, `Spans`, `Bounds`, `Caret`, `Wrap`.

## Components

| Component (script name) | Key fields | What it does |
|---|---|---|
| `Font` | `path`, `atlas_pages` (4) | TrueType/OpenType file, relative to cwd or an ancestor. Loads when a `Text` first uses it |
| `FontVariants` | `bold`, `italic`, `bold_italic` (Font entities, 0 = none) | Faces for styled spans; missing ones are synthesized |
| `FontStatus` (read only) | `state` (`Loading`/`Ready`/`Failed`), `revision`, `error` | Load outcome; `revision` increments per successful load |
| `Text` | `value`, `size` (16), `line_height` (0 = font's), `width` (0 = break at newlines only), `align` (`Left`/`Center`/`Right`), `font` (`box.text.DefaultFont`), `wrap` (`Words`/`Newlines`), `hang` | The string, UTF-8, in logical pixels in local XY, facing +Z, block centered on the origin, y up |
| `TextSpans` (`Spans`) | `spans: [{start, end, color, bold, italic, keep_color}]` | Byte ranges with own color/style; later spans win |
| `TextBounds` (`Bounds`, written) | `width`, `height` | Block size; 0 while the font is not loaded |
| `TextCaret` | `x`, `y`, `height`, `line` | Result type of `box_text_caret` |

Color, opacity and emission come from `Rgb`, `Opacity`, `Emissive`, `Tint`, `BlendMode` on the
entity. `Text` implies `TextBounds` and a private `TextGlyphs`.

Default fonts (B612): `box.text.DefaultFont`, `DefaultFontBold`, `DefaultFontItalic`,
`DefaultFontBoldItalic` (`etc/fonts/B612-*.ttf`); `DefaultFont` has `FontVariants` pointing at
the other three.

## Flecs script

```flecs
using box.transform
using box.material
using box.text

font_bold { Font: {path: "test/assets/fonts/B612-Bold.ttf"} }
font_body {
  Font: {path: "test/assets/fonts/B612-Regular.ttf"}
  FontVariants: {bold: font_bold}
}

status {
  Text: {value: "Output 82%   Coolant low", size: 22, font: font_body}
  Rgb: {250, 210, 90}
  Spans: {spans: [{start: 13, end: 24, color: {250, 110, 80, 255}, bold: true}]}
}

// World-space text: 1 logical pixel = 1 unit, so scale it down
label {
  Position: {0, 2, 0}
  Scale: {0.006, 0.006, 0.006}
  Text: {value: "Hello", size: 48, width: 300, align: Center}
  Rgb: {255, 255, 255}
}
```

In a screen-space UI (ortho camera, `ortho_scale: 1`) a logical pixel is one point.

## C API

| Function | Use |
|---|---|
| `box_text_caret(world, e, offset, &caret)` | Caret line at a byte offset (false while font not loaded) |
| `box_text_hit(world, e, x, y)` | Byte offset nearest a local point |
| `box_text_line(world, e, line, &start, &end)` | Byte range of a line; returns the line count |
| `box_text_break_lines(world, &text, &starts)` | Line starts of a `Text` value without an entity (measures, no atlas use) |
| `box_text_break_lines_spans(world, &text, &spans, &starts)` | Same, with span styles |

`starts` is an initialized `ecs_vec_t` of `int32_t` (cleared first) or NULL to only count.

## Behavior rules

- Setting a `Text` lays it out at once. A font that loads, or an atlas page that grows, lays out
  every text of that font (and of fonts naming it in `FontVariants`) again.
- A new `Font.path`, or a file change FileWatch sees, reloads the font; the old one stays in use
  until the new one is ready.
- Inside a layout, `wrap: Words` breaks lines at the width the layout gives; `Newlines` keeps the
  lines of `Text.width` (Clay's wrap none). The widest word is the element's content minimum.
- Offsets in spans, carets and `hang` are byte offsets in UTF-8, not characters. Span ranges are
  clipped to the string, so `end: 255` means "to the end" for short strings.
- Color-only spans do not change layout; bold/italic spans do (variant face advances, wider
  synthesized bold). The italic lean is not in `TextBounds`.
- Style face lookup: bold italic -> `bold_italic`, else `bold` + synthesized italic, else
  `italic` + synthesized bold, else the base font with both synthesized. A variant loads on
  first use; until then the glyph falls back to the next row and lays out again when loaded.
- Ascent, descent, line height and baseline always come from the base font.
- Glyph edges blend in linear light; dark text gets fuller edges to look as heavy as light text
  (source-over blend modes only).
- `ClipChildren` on an ancestor clips text to its area.
- Headless frames wait (up to 10 s) for font loads and glyph jobs in flight, so the first frame
  already has text.

## Gotchas

- Script name collisions: `Bounds` exists in `box.text` and `box.layout`, `Wrap` in `box.text`
  and `box.texture`. With both modules in `using`, qualify: `layout.Bounds`, `texture.Wrap`.
- `TextBounds` is 0 until the font has loaded; don't size things from it in the first frame of a
  windowed run.
- `font: 0` means no font: nothing is drawn and bounds stay 0.

## Atlas and cost

- Glyph fields are 40 texels per em, distance range 4 texels, generated by jobs when a layout
  first uses a glyph. Pages start at 256x256 and grow to 1024x1024 before a new page opens.
- Pages beyond `Font.atlas_pages` that no visible `Text` shows are released least-recently-used
  first; their glyphs are regenerated on next use. Only shown pages may exceed the limit.
- Systems: `box.text.Frame` (`EcsPreFrame`), `Refresh` and `Trim` (`EcsOnStore`).

## Tests

```sh
bake3 run test/cpu/ui --local-env -- Msdf          # also TextCaret, TextSpans, Wrap
bake3 run test/gpu/ui --local-env -- Text          # also TextLines, TextSpans, TextField
BOX_MSDF_WRITE_REFERENCE=1 bake3 run test/cpu/ui --local-env -- Msdf
```

The last line rewrites the MSDF reference fields `test/assets/msdf/*.rgba` (raw RGBA8, 40x40,
one per `Msdf` test case) from the current generator; then check the suite passes without it.
