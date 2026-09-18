# Blending and translucency

Blend modes, back face handling and the roughness driven translucency used for milk glass and blurred panels.

## Details

BlendMode selects the blend equation per material and moves the instance into a sorted transparent pass drawn after opaque geometry. Opacity combines with PbrMaterial.roughness so a translucent surface blurs what is behind it, implemented by sampling a mip chain of the opaque color target, which is the milk glass effect the brief asks for. BackFaceMode decides whether back faces show artwork, plain material constants or nothing.

Verification: unit tests cover pass assignment, sort order, the blend mode enum and the back face modes. Visual scenes render overlapping transparent panels, a milk glass panel over detailed geometry and each back face mode, and a benchmark measures the sorted transparent pass with 10k transparent instances.
