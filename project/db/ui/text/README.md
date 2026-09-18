# Text and text helpers

Text rendering plus the measuring and building helpers scripts use to make text widgets.

## Details

Text holds a string, a line height and an alignment; the module rasterizes a font atlas, lays out glyphs with wrapping and emits the glyph quads as instances. The helpers the brief asks for are exposed as components and script functions for measuring a string, wrapping it to a width and creating a text element inside a layout, so a widget can be built entirely from script. The font is a component pointing at a font file, and the atlas grows on demand.

Verification: unit tests cover measurement, wrapping, alignment, atlas growth and font switching. A visual scene renders paragraphs at several sizes and alignments, and a script test builds a labelled button from script only, proving the helpers are usable there.
