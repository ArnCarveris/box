# Cascaded shadows

Cascaded shadow maps for the directional light with per view parameters and a static and dynamic caster split.

## Details

Cascades are computed per view from the camera frustum with configurable split distances, depth bias, normal offset and filter size on the view. The shadow pass reuses the static and dynamic instance split: static cascade content is only re-rendered when a cascade moves or a static caster changes, while dynamic casters are drawn every frame into the same maps. NoShadow keeps an entity out of the pass and shadow_layers restricts the caster set. Sampling uses a PCF filter sized by the cascade.

Verification: unit tests cover split computation, cascade bounds, the caster set and the invalidation of cached static cascades. Visual scenes render a hierarchy under a directional light at several sun elevations, and a benchmark with 100k static and 10k dynamic casters proves the cache keeps the frame inside the 60 FPS budget and measures a full rebuild.

Introduces ShadowSettings on View and honours NoShadow from the material datamodel.
