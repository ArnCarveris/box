# Yeon

A kite fight (연싸움) over a Joseon village. You fly a bangpae-yeon, the Korean shield kite with
a hole in its center and a red kkokji circle, above hanok rooftops, pine trees, a stone pagoda and
layered mountains. Catch the glowing cheongsachorong lanterns and keep away from the rival kites
in the five traditional colors: each one that touches your kite cuts a string. Three cut strings
and your kite falls.

```sh
bake3 run apps/yeon --local-env
```

| Key          | Action                        |
|--------------|-------------------------------|
| Up / W       | Pull the string: climb        |
| Down / S     | Let out string: dive          |
| R            | Fly again after the last cut  |

Every 5 lanterns the wind picks up and the village scrolls faster.

The game lives in `src/yeon.c` (flight, spawning, collisions) and the scene in `etc/`:
`config.flecs` holds the tuning constants, `kites.flecs` the kites and lanterns, `scenery.flecs`
and `world.flecs` the village, `run.flecs` the state of a run (reloaded by R) and `hud.flecs` the
overlay.
