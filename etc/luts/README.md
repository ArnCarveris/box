# Lookup tables

`tony_mc_mapface.dds` is the Tony McMapface tone mapping LUT, Copyright (c) 2023 Tomasz
Stachowiak (https://github.com/h3r2tic/tony-mc-mapface, `shader/tony_mc_mapface.dds` at commit
0f249d3), licensed under the MIT license, see `LICENSE-tony-mc-mapface.txt`.

It is a 48x48x48 volume of RGB9E5 texels after a 148 byte DDS header (DXGI format 67,
`R9G9B9E5_SHAREDEXP`), x fastest, then y, then z. The texel at (r, g, b) holds the display
color of the input `c / (c + 1)` sampled at `(r, g, b) / 47`, linear. A `Tonemap` with
`operator_: TonyMcMapface` loads it into a 3D texture the tone map pass samples with linear
filtering.

It ships in the framework `etc` folder, so the wasm build embeds it.
