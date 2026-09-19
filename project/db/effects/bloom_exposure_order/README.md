# Bloom after exposure

Bloom runs before auto exposure so its firefly clamp is in scene units; order it after exposure so the clamp and intensity are exposure relative.

## Details

The showcase found that the built in bloom sits before the exposure pass in the stack order, so its firefly clamp and knee are expressed in scene radiance and a dark scene brightened by auto exposure blooms differently from a bright one at the same final brightness. Reorder the built in stack so exposure multiplies the color first, then bloom, then tone mapping; keep the parameter meanings and re-baseline the bloom, showcase and tone map scenes with the reason recorded.

Verification: an effects test on the built in order, the re-baselined scenes, and the bloom cost unchanged.

## References

- `project/db/effects/showcase/deliverable/README.md`
