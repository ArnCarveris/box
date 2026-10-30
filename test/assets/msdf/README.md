# MSDF reference fields

Raw RGBA8 texels, 40x40, that `box_msdf_generate` gives for the shapes of the `Msdf` suite in
`test/cpu/ui/src/Msdf.c`, one file per test case. Run the suite with `BOX_MSDF_WRITE_REFERENCE=1`
to write them anew from the current generator, then check that the suite passes without it.
