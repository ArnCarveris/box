# Tint opacity and composition

- `Tint.a` is the tint strength: views mix the surface color toward `Tint.rgb`
  by it, 0 leaves the color and 255 replaces it. Transparency is `Opacity`
  alone; the tint alpha no longer moves an instance to the sorted pass.
- `src/material/tint.c` resolves a subtree from the root down through the
  hierarchy helper, so ChildOf, Parent and mixed trees behave alike. Strength
  `a` over `(color, alpha)` gives `alpha' = 1 - (1 - alpha)(1 - a)` and `color'
  = (color * alpha * (1 - a) + tint * a) / alpha'`, kept in `ActualTint` bytes.
- Every entity of a tinted subtree gets `ActualTint`, its own `Tint` included.
  Setting, changing or removing a tint re-resolves that subtree; reparenting in
  either storage re-resolves the entity that moves.
- Extract mixes once into the instance color the shader already reads,
  `mix(rgb, ActualTint.rgb, ActualTint.a)`, with the opacity as its alpha.
- 28 `Material` cases (`tests.json`), 223 render tests, 611 in total, lint 0.
  Nothing in `bench.json` regresses, extract runs 4 to 10% faster.
- `tint.png`: primaries at 50% on single boxes and four trees (ChildOf, Parent,
  mixed both ways) with a blue 50% parent over a black 50% child, so every
  grandchild is the same `{0, 0, 85, 192}` darker blue.
