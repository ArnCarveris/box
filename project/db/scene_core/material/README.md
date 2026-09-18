# Material module

Rgb, Opacity, PbrMaterial, Emissive, Tint with recursive resolution and DynamicMaterial.

## Details

Colors are display encoded bytes converted to linear through a lookup table when instances are extracted. `Opacity` defaults to one and `PbrMaterial` to roughness one half through constructors. `Tint` resolves into `ActualTint` on the entity and every descendant without its own tint; the observers queue the writes on the observer stage because this flecs has no implicit defer mode. New children of a tinted parent inherit the resolved tint through an observer with an up traversal term. `DynamicMaterial` marks a subtree for per frame extraction.
