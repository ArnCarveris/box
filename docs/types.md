# Types

`box.types` (`src/types`, `include/box/types.h`) registers the reflected math and color types
every box module uses in its components, owned-memory lifecycle hooks for reflected components,
and UTF-8 helpers. Most modules import `BoxTypes` themselves.

## Types

| Type | Layout | Script value |
|------|--------|--------------|
| `vec2` | cglm `float[2]`, members `x y` | `{1, 2}` |
| `vec3` | cglm `float[3]`, members `x y z` | `{6.9, 3, 6}` |
| `vec4` | cglm `float[4]`, members `x y z w` | `{0, 0, 0, 1}` |
| `mat4` | cglm `float[4][4]`, member `v` (array of 16 floats) | rarely written in scripts |
| `rgb` | `uint8_t r, g, b`, display encoded | `{255, 120, 40}` |
| `rgba` | `uint8_t r, g, b, a`, display encoded | `{10, 12, 20, 255}` |

They are types for members, not components you add to entities (the color component is `Rgb` in
box.material).

```flecs
ReflectionProbe: {extent: {6.9, 3, 6}, offset: {0, -0.2, 0}}   // vec3 members
View: {camera: camera, clear: {enabled: true, color: {10, 12, 20, 255}}}  // rgba member
```

## Owned memory hooks

`ECS_META_COMPONENT` gives C structs no dtor/copy/move, so a component with a `char*` or
`ecs_vec_t` member that it owns installs hooks right after registering it, before first use:

```c
ECS_META_COMPONENT(world, App);
box_owned_hooks(world, App, BOX_OWNED_STR(App, scene), BOX_OWNED_STR(App, camera),
    BOX_OWNED_STR(App, out));

ECS_META_COMPONENT(world, Curve);
box_owned_hooks(world, Curve, BOX_OWNED_VEC(Curve, keys, CurveKey));
```

| Macro / function | Use |
|------------------|-----|
| `BOX_OWNED_STR(T, member)` | Owned `char*`: freed, `strdup`-ed on copy |
| `BOX_OWNED_VEC(T, member, E)` | Owned `ecs_vec_t` of trivially copyable `E`: freed, copied on copy |
| `box_owned_hooks(world, T, ...)` | Installs ctor (zero), dtor, copy, move from the owned members |
| `box_types_owned_hooks(world, id, members, count)` | Function form; `count > 0` |
| `BOX_VEC_VIEW(T, ...)` | Literal array as an `ecs_vec_t` that owns nothing (C only) |
| `BOX_VEC_LITERAL_COUNT(T, ...)` | Element count of a literal array |

```c
ecs_set(world, e, Curve, { .keys = BOX_VEC_VIEW(CurveKey, {0, 1}, {1, 2}) });
```

Gotchas:

- `BOX_VEC_VIEW` is only safe through a copy (`ecs_set`, `ecs_set_ptr`): the copy hook duplicates
  it. Never write it into a component in place (`ecs_ensure`/`ecs_get_mut`): the dtor would free a
  stack array.
- Strings/vectors assigned in place to an owned member must be heap allocated (`ecs_os_strdup`,
  `ecs_vec_*`) and the old value freed first.
- A vec member with a NULL array copies as an empty, zeroed vec.

## UTF-8

| Function | Use |
|----------|-----|
| `int32_t box_utf8_next(const char **str)` | Decode one code point and advance; ill-formed sequences (overlong, surrogates, > U+10FFFF) read as `BOX_UTF8_REPLACEMENT` (U+FFFD), one per maximal subpart; never passes the terminating zero |
| `int32_t box_utf8_encode(int32_t code, char *out)` | Encode into `out` (>= 4 bytes), returns 1..4; invalid code points encode as U+FFFD |

Used by input, console, text and ui text editing.

## Other

- `BOX_LOD_LEVELS_MAX` (4): most levels of detail of a shape mesh, level 0 included.
- The browser build needs wasm SIMD: `src/types/types.c` errors at compile time without
  `-msimd128` (cglm's wasm SIMD path).
