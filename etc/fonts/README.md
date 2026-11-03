# Fonts

`B612-Regular.ttf`, `B612-Bold.ttf`, `B612-Italic.ttf` and `B612-BoldItalic.ttf` are B612
Regular, Bold, Italic and Bold Italic 1.008, Copyright 2012 The B612 Project Authors
(https://github.com/polarsys/b612), licensed under the SIL Open Font License 1.1, see `OFL.txt`.
The variants come from https://github.com/google/fonts/tree/main/ofl/b612 and share the metrics
of the regular face: 2000 units per em, an ascent of 1930, a descent of -500 and no line gap.

They ship in the framework `etc` folder, so the wasm build embeds them. A `Text` uses the regular face
by default, through the `box.text.DefaultFont` entity, whose `FontVariants` name
`box.text.DefaultFontBold`, `box.text.DefaultFontItalic` and `box.text.DefaultFontBoldItalic`.
A variant loads only once a styled span needs it.
