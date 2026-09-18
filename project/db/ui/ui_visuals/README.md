# Box shadows and group transparency

BoxShadow and parent transparency that composites a subtree as one image without ghosting.

## Details

BoxShadow draws a blurred, offset, spread shadow behind an element using the same analytic rounded rectangle as Border. Group transparency renders a subtree to a texture and composites it with the parent opacity, so children do not show through each other, which is what the brief requires of transparent panels. The render to texture path is only taken for subtrees whose opacity is below one, so the common case costs nothing.

Verification: unit tests cover the trigger condition, target sizing, nesting and the shadow parameters. Visual scenes compare a transparent panel composited through the texture path with the naive direct path, where the direct path must show the ghosting the texture path removes, and a benchmark measures ten transparent panels.
