#ifndef FLECS_ENGINE_SURFACE_EFFECTS_H
#define FLECS_ENGINE_SURFACE_EFFECTS_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_SURFACE_EFFECTS_IMPL
#define ECS_META_IMPL EXTERN
#endif

/* Surface effects: a layer on top of the resolved PBR material.
 *
 * A base material describes what an object *is* (paint, plastic, rusted
 * iron). A surface effect describes what *happened* to it: dust settled on
 * its upward faces, grime built up in the low and sheltered parts, a cloud
 * drifted over it. Effects are applied in the PBR fragment shaders after
 * the base material is resolved (see shaders/common/surface_wgsl.h) and
 * before lighting, so they work identically for primitives, glTF meshes,
 * bevel boxes and terrain.
 *
 * Effects come in two flavours:
 *
 * - Per object: FlecsSurfaceWear carries how weathered and how burnt a single
 *   entity is. Its values ride in the unused w lanes of the per-instance
 *   transform, so an object can be dusty without owning a material: no
 *   material id is allocated, no batch is split, and instances with
 *   different wear still share one indirect draw. There are seven values and
 *   four lanes: dust, grime and burn each keep a lane, since a game animates
 *   them, and scale, seed, edge and scratch - all coarse by nature - share
 *   the third one quantized (see flecsEngine_batch_wearInstance). The noise
 *   every layer is masked by is sampled in the object's own frame, scaled to
 *   world units, so a pattern travels with the object it is on; two instances
 *   of one prefab are decorrelated by an offset picked from where the object
 *   was first drawn and SurfaceWear.seed - from the scene's own data, so that
 *   the same scene weathers the same way on every build, and kept for the
 *   life of the object, so that driving it away does not re-weather it.
 *
 * - Per view: FlecsRenderView.surface_effects describes what the wear in this
 *   scene *looks* like, and FlecsRenderView.cloud_shadow adds an animated
 *   noise field that darkens everything by world XZ position. Both live on the
 *   view rather than on the world, exactly the way FlecsRenderView.shadow
 *   does, and both are read into that view's frame uniform every frame. Two
 *   views of the same world can therefore weather it differently - a main
 *   camera with the full set and a cheap minimap view with all of it off.
 *
 * surface_effects has seven wear layers, applied in this order:
 *
 *   grain    - fine particulate speckle, the tooth that survives at
 *              gameplay distance (scaled by the object's dust amount)
 *   dust     - pale accumulation on upward faces (dust amount)
 *   grime    - dark buildup low down and on sheltered faces (grime amount)
 *   streak   - vertical dirt runs down tall faces (grime amount)
 *   scratch  - thin anisotropic lines exposing the material under the paint
 *   edge     - paint knocked off edges, bevels and corners
 *   burn     - soot blotches that char the surface (burn amount)
 *
 * and an eighth layer, wetness, which is the one whose numbers do not live
 * here: rain falls on the world, not on a view, so FlecsWetness (below) holds
 * how wet the scene is and FlecsWeather keeps integrating it, while
 * surface_effects.wetness only decides whether this view draws it. That split
 * is what lets a cheap minimap thumbnail render the same rained-on city dry.
 *
 * scratch and edge are keyed on max(dust, grime) - "this object is weathered
 * at all". A scratch is also keyed on the base material's metallic, so bare
 * steel scratches brightly and concrete only scuffs; a chip is not, because
 * paint knocked off a steel plate is more visible than paint knocked off a
 * concrete one, not less. Both are painted-surface effects, though, and a
 * scene has things in it that were never painted - a boulder, a dune, an ore
 * outcrop - which dust and grime like anything else but which must not chip
 * or show bare metal, so each takes a per-object switch as well:
 * FlecsSurfaceWear.edge and FlecsSurfaceWear.scratch. Neither layer is drawn
 * unless the view switches it on, so a scene that opts into dust and grime
 * and stops there never chips and never scratches. burn is keyed on its own
 * per-object amount instead, which is what lets a game drive it from a
 * damage number without touching the weathering the scene was tuned for.
 *
 * Every layer, and the cloud shadow, carries an `enabled` switch that reads
 * the way FlecsRenderView.shadow.enabled does. It is not a strength of zero
 * dressed up as a boolean: switching a layer off removes its branch's noise
 * taps from the fragment shader outright, so turning layers off is a real way
 * to buy frame time on a machine that needs it. The layer keeps the numbers
 * the scene authored while it is off, so switching it back on restores the
 * look without the scene having to remember them.
 *
 * Every one of those switches is off by default, which makes the layers
 * opt-in: a layer is drawn where a view says `enabled: true` and nowhere
 * else. That is the rule that makes deleting a layer's block from a scene
 * switch the layer off - the numbers around it come from the constructor
 * either way, so a block that only carried numbers left the effect rendering
 * on the engine's defaults after it was removed. surface_effects has a master
 * `enabled` above the eight which skips the whole pass in one uniform-valued
 * branch; that one starts true, because it is a kill switch over whatever the
 * scene opted into rather than an opt-in of its own, and it leaves the
 * per-layer flags untouched so switching it back on restores the scene's set.
 * Wetness answers to that master too: a view draws it only where
 * `enabled && wetness.enabled && FlecsWetness` has water in it.
 *
 * Objects without FlecsSurfaceWear render as they would with no weathering at
 * all - the shader early-outs on a zero wear amount - and a view that never
 * mentions surface_effects or cloud_shadow renders none of either, because
 * every layer under it is off.
 *
 * There is a third switch, and it is per material: FlecsSurfaceEffects (see
 * modules/material.h) says which of these layers are allowed to touch a
 * material at all. It is off for everything by default, so a material that
 * never mentions it takes part in nothing however the view is configured -
 * which is what lets rain wet a road and leave the pavement beside it dry
 * without either of them owning a copy of the weather. The view still owns
 * what each layer looks like; the material only says whether it is in.
 *
 * Participation costs a material nothing to decline: the switches travel as
 * a bit per layer in the material record, and every layer tests its bit next
 * to the view's own switch ahead of its noise taps, so opting out is the
 * same uniform-valued compare that switching the layer off on the view has
 * always been. One shader draws them all.
 *
 * Example scene:
 *
 *   view {
 *     RenderView: {
 *       camera: camera,
 *       shadow: { enabled: true, map_size: 4096 },
 *       surface_effects: {
 *         scale: 1.5
 *         range_fade: 0.3
 *         dust:  { enabled: true, color: {196, 184, 156}, sharpness: 3,
 *                  roughness: 0.95 }
 *         grime: { enabled: true, color: {90, 82, 70}, threshold: 0.45,
 *                  height: 2.5, falloff: 3, roughness: 0.8 }
 *         scratch: { enabled: true, color: {198, 204, 214}, amount: 0.9,
 *                    scale: 7, width: 0.26, roughness: 0.25, metallic: 0.9,
 *                    max_range: 85 }
 *         grain:   { enabled: true, amount: 0.45, scale: 0.12,
 *                    roughness: 0.35, max_range: 70 }
 *         streak:  { enabled: true, amount: 0.55, scale: 4, length: 0.7,
 *                    height: 1.3, threshold: 0.55 }
 *         edge:    { enabled: true, amount: 0.55, onset: 4, full: 18,
 *                    breakup: 0.7, width: 0.04, color: {214, 216, 222},
 *                    contrast: 0.85, softness: 0.35 }
 *         burn:    { enabled: true, color: {22, 18, 16}, amount: 1,
 *                    scale: 1.6, softness: 0.28, roughness: 0.9, ember: 0 }
 *         wetness: { enabled: true }
 *       },
 *       cloud_shadow: {
 *         enabled: true, strength: 0.55, scale: 90, coverage: 0.5,
 *         softness: 0.18, speed: 3, direction: 0.6
 *       }
 *     }
 *   }
 *
 * Switching a single layer off again, from a scene or over the REST API, is
 * one member deep and leaves everything around it alone - which is also how a
 * settings menu turns one layer off without losing what the scene authored:
 *
 *   view {
 *     RenderView: { surface_effects: { scratch: { enabled: false } } }
 *   }
 *
 *   crate {
 *     Position3: {0, 1, 0}
 *     Box: {2, 2, 2}
 *     Rgba: {180, 140, 90}
 *     SurfaceWear: { dust: 0.8, grime: 0.3, burn: 0.4 }
 *   }
 *
 *   lamp_glass {
 *     Position3: {0, 2.4, 0}
 *     Box: {0.6, 0.6, 0.6}
 *     Rgba: {180, 220, 255}
 *     Emissive: {strength: 3}
 *     SurfaceWear: { dust: 0.8, edge: 0 }
 *   }
 *
 *   boulder {
 *     Position3: {4, 0.5, 0}
 *     Box: {1.2, 1, 1.1}
 *     Rgba: {130, 98, 70}
 *     SurfaceWear: { dust: 0.6, edge: 0, scratch: 0 }
 *   }
 */

/* Per-object weathering amounts. Inherited on instantiation, so a prefab
 * can define a default that instances override. Uploaded into the .w lanes
 * of the instance transform; an all-zero component costs one compare in the
 * fragment shader. */
ECS_STRUCT(FlecsSurfaceWear, {
    float dust;   /* 0..1 dust coverage on upward-facing surfaces */
    float grime;  /* 0..1 grime buildup in low / sheltered surfaces */
    float scale;  /* multiplies the physical feature size of every layer -
                   * the dust and grime cell, the grain cell, the streak
                   * spacing and run length, the scratch row pitch, the edge
                   * breakup and the burn blotch. 0 means 1. The size a layer
                   * lands at is always in metres: the wear is sampled in the
                   * object's own frame scaled to world units, so a 2 m crate
                   * and a 30 m wall carrying the same material wear at the
                   * same physical size, and this is the only knob that
                   * changes it. */
    float seed;   /* 0..1, offsets the noise so identical objects differ */
    float burn;   /* 0..1 fire damage; drive it from health, hull, heat */
    float edge;   /* 0..1 multiplier on the edge layer; 1 (the default) is
                   * what every part did before this existed, 0 keeps a part
                   * from ever chipping however dusty it is - glass, emissive
                   * crystal, anything whose silhouette is the point of it.
                   * Quantized to 15 levels in the packed lane. */
    float scratch; /* 0..1 multiplier on the scratch layer; 1 (the default)
                    * scratches as the scene says, 0 never scratches - rock,
                    * sand, wood, anything that has no paint to scrape
                    * through and no metal to expose. Packed as a single
                    * bit, so it rounds to on or off at 0.5. */
});

extern ECS_COMPONENT_DECLARE(FlecsSurfaceWear);

/* Every layer below leads with the same switch. `enabled` defaults to false
 * and gates the layer's branch in the fragment shader, so a layer nobody asked
 * for costs one compare against a uniform-valued flag and not one noise tap,
 * and a scene turns an effect on by naming it and off by deleting it. The
 * switch is deliberately separate from the amount a layer already has: an
 * amount is what the scene was tuned to, a switch is what the scene, a
 * settings menu or a benchmark flips, and neither should have to destroy the
 * other. */
typedef struct {
    ecs_bool_t enabled;  /* the default, false, skips the dust branch */
    flecs_rgba_t color;  /* color dust converges to */
    float sharpness;     /* up-facing exponent; higher = flatter faces only */
    float roughness;     /* roughness dust converges to */
} FlecsSurfaceDust;

typedef struct {
    ecs_bool_t enabled;  /* the default, false, skips the grime branch */
    flecs_rgba_t color;  /* multiplied into albedo, so grime darkens/tints */
    float threshold;     /* 0..1 noise coverage threshold */
    float height;        /* world Y at and below which grime collects */
    float falloff;       /* world units over which the height mask fades */
    float roughness;     /* roughness grime converges to */
} FlecsSurfaceGrime;

/* Scratches and edge wear: the material *under* the paint, revealed where
 * the object was scraped. A scratch is a straight, tapered segment with a
 * random direction, length and depth; they come in clusters, with clean
 * surface between. Drawn after dust and grime so a scratch cuts through the
 * dirt instead of collecting it. amount is how many there are, not how
 * faint: it is scaled per object by the larger of that object's dust and
 * grime amounts and every scratch that exists is drawn whole, while the
 * base material's metallic scales how brightly it shows, so bare steel
 * scratches brightly and concrete only scuffs. */
typedef struct {
    ecs_bool_t enabled;  /* the default, false, skips the scratch branch */
    flecs_rgba_t color;  /* color a scratch exposes */
    float amount;        /* 0..1 scratch density; 0 disables */
    float scale;         /* scratch rows per world unit; a scratch is up to
                          * about five rows long */
    float width;         /* 0..1 line thickness, as a fraction of a row */
    float roughness;     /* roughness inside a scratch */
    float metallic;      /* metallic inside a scratch */
    float max_range;     /* world units past which the layer is not drawn at
                          * all; 0 (the default) never cuts. See the note on
                          * distance cuts below. */
} FlecsSurfaceScratch;

/* Fine particulate speckle, an order of magnitude smaller than the dust
 * patches. This is the layer that survives at gameplay distance: it gives
 * the surface a per-pixel tooth instead of a flat recolor, and it breaks up
 * the edge of the dust coverage. Scaled per object by the dust amount and
 * faded out automatically once a grain cell is smaller than a pixel. */
typedef struct {
    ecs_bool_t enabled; /* the default, false, skips the grain branch */
    float amount;     /* 0..1 speckle contrast; 0 disables */
    float scale;      /* world units per grain cell */
    float roughness;  /* how much the speckle jitters roughness */
    float max_range;  /* world units past which the layer is not drawn at all;
                       * 0 (the default) never cuts. See the note on distance
                       * cuts below. */
} FlecsSurfaceGrain;

/* Vertical dirt runs. Where grime is a band at the bottom of a form, streaks
 * are the drips that start high on a wall and run down it, so a tall face is
 * not clean above the grime line. Uses the grime color and the grime amount. */
typedef struct {
    ecs_bool_t enabled; /* the default, false, skips the streak branch */
    float amount;     /* 0..1 run strength; 0 disables */
    float scale;      /* runs per world unit across a face; divided by
                       * FlecsSurfaceWear.scale like every other layer's
                       * frequency */
    float length;     /* world units a run stretches vertically; multiplied
                       * by FlecsSurfaceWear.scale */
    float height;     /* world Y at which runs start */
    float threshold;  /* 0..1 noise coverage threshold */
} FlecsSurfaceStreak;

/* Paint knocked off edges and corners. Two masks, maxed together.
 *
 * On a bevel box or a curved rim the mask is a screen-space proxy: the
 * world-space rate of change of the *geometric* normal, which spikes on a
 * bevel strip and stays near zero on a flat face. It deliberately ignores
 * normal and interior maps: those turn the shading normal at a rate set by
 * the material's UV layout, which would make the chips a picture of the
 * object's texture coordinates instead of of its shape. That proxy is blind to a *sharp*
 * edge - a derivative never leaves the triangle it was taken in, and a box
 * face has one constant normal - so on the primitives most game geometry is
 * built from the distance to the face border is computed outright instead,
 * from the fragment's object-local position and the instance's scale, and
 * anything within `width` world units of a border chips. Both masks are
 * broken up by noise so an edge chips in places rather than reading as an
 * outline, and scaled per object by FlecsSurfaceWear.edge.
 *
 * `amount` is coverage, not opacity: it moves the threshold the breakup noise
 * is cut at, so a low amount chips a few corners and a high one chips most of
 * the edge, and wherever the paint *is* gone it is gone by `contrast` - the
 * same trick the burn layer plays with its blotch threshold. A chip that only
 * mixes a fraction of the way to its color is the one thing that does not
 * read as a chip at gameplay distance; it reads as a soft outline.
 *
 * Which way a chip contrasts is decided per fragment from the base color: a
 * dark surface chips to `color` (bare metal under the paint) and a light one
 * to a deep notch in its own color, so the layer stays visible on a light
 * hull and a dark one without the scene picking a side. */
typedef struct {
    ecs_bool_t enabled; /* the default, false, skips the edge branch */
    float amount;   /* 0..1 fraction of the edge that chips; 0 disables */
    float onset;    /* proxy curvature (1 / world units) where wear starts */
    float full;     /* proxy curvature at which wear is at full strength */
    float breakup;  /* 0..1 how much noise eats into the edge line */
    float width;    /* world units of the exact border band on primitives.
                     * A physical chip width, so it is not scaled by
                     * FlecsSurfaceWear.scale; that scale moves how far apart
                     * the chips sit along the edge, not how deep they bite */
    flecs_rgba_t color;  /* color a chip exposes on a dark base; defaults to
                          * the scratch layer's default color */
    float contrast; /* 0..1 how far a chip pushes the base color to the chip
                     * color; 1 replaces it outright */
    float softness; /* 0..1 fraction of the band that feathers; the rest of
                     * the band reads solid. Always at least a pixel wide, so
                     * a distant chip softens instead of crawling */
    float max_range; /* world units past which the layer is not drawn at all;
                      * 0 (the default) never cuts. See the note on distance
                      * cuts below. */
} FlecsSurfaceEdge;

/* Fire damage: soot blotches that char the surface. Unlike the layers above
 * this one is not weathering, so it is not keyed on dust or grime: it has its
 * own per-object amount, meant to be driven by whatever the game already
 * tracks (health, hull integrity, heat). The blotch threshold moves with that
 * amount, so a lightly damaged object gets a few scorch marks and a nearly
 * destroyed one is charred all over. Applied last: a burn sits on top of the
 * dirt and the scratches, and scorched metal is no longer shiny. */
typedef struct {
    ecs_bool_t enabled;  /* the default, false, skips the burn branch */
    flecs_rgba_t color;  /* soot color a fully charred surface reaches */
    float amount;        /* scene multiplier on the per-object burn; 1 = as
                          * authored, 0 disables the layer scene-wide */
    float scale;         /* world units per blotch */
    float softness;      /* 0..1 blotch edge feather */
    float roughness;     /* roughness inside a burn */
    float ember;         /* 0..1 warm glow in the hottest cores; 0 disables */
} FlecsSurfaceBurn;

/* The view's half of the wetness effect. Everything wetness *is* - how much
 * water is down, how much of it stands in pools, how fast it dries - lives in
 * FlecsWetness on the world, because rain falls on the world and one weather
 * system integrates it for every view at once. What is left for a view to say
 * is whether it draws any of it, which is this switch and nothing else: there
 * is no per-view look here because there is no number in the wetness shader
 * that one view would want different from another's while looking at the same
 * street. Off by default like the seven wear layers, so a view renders a
 * rained-on world dry until it asks for the water - which is what keeps a
 * minimap thumbnail off the pool noise, the ripple taps and the SSR the pools
 * would otherwise fire. */
typedef struct {
    ecs_bool_t enabled;  /* the default, false, skips the wetness branch */
} FlecsSurfaceWetness;

/* What the per-object wear channels look like, for one view. Carried by value
 * in FlecsRenderView.surface_effects, not as a component of its own: it is
 * configuration for a render, the way flecs_engine_shadow_params_t is, and a
 * view that never mentions it gets the defaults below from the render view's
 * constructor.
 *
 * All eight layers are off in those defaults, so the block a view does not
 * write is the block that renders nothing. The scratch, grain, streak and edge
 * layers additionally sit at an amount of 0, because for those four the amount
 * is the layer's strength and there is no strength the engine can pick for a
 * scene it has not seen; burn's amount defaults to 1 instead, since it is a
 * multiplier over a per-object amount that is 0 until a game drives it. */
ECS_STRUCT(flecs_engine_surface_effects_t, {
    ecs_bool_t enabled;  /* master switch over all eight layers, and the one
                          * switch here that defaults to true: it is a kill
                          * switch over what the scene opted into, not an
                          * opt-in of its own. False skips the whole pass on
                          * one uniform-valued compare, which is the cheapest
                          * the fragment shader can get without compiling a
                          * second pipeline, and leaves the per-layer flags
                          * alone so switching it back on restores the set the
                          * scene configured. */
    float scale;  /* world units per noise cell */
    /* Distance cuts. scratch, grain and edge each carry a `max_range`: past
     * that many world units from the camera the layer's noise taps are not
     * evaluated at all, the same way an `enabled` of false skips them, except
     * that the decision is made per fragment instead of per view. 0, the
     * default, never cuts, so a scene that says nothing renders exactly as it
     * did before the knob existed - which is also how a benchmark separates
     * this from everything else: set every max_range to 0.
     *
     * These three are the layers worth cutting, and the order is by what they
     * cost, not by what they are: scratch is the most expensive fragment work
     * in the engine (four rotated row grids per triplanar plane, each testing
     * three rows), grain is the next, and edge is a distant third because its
     * border test already rejects most of a face before it taps anything.
     * Dust and grime deliberately have no cut: they are two taps each, and
     * they are what still says "weathered" once a building is forty pixels
     * tall, so cutting them costs the read that the whole system exists for
     * and buys the least of any of the seven.
     *
     * A cut is not the same thing as the per-layer LOD terms next to it.
     * `line_lod` and `grain_lod` fade a layer out once its own features stop
     * being resolved - once a scratch is thinner than a pixel - and they are
     * right about that: in a game whose camera sits tens of units above the
     * ground they never reach their fade, because a scratch at that distance
     * is still several pixels long. A max_range says something the shader
     * cannot work out on its own: that past some distance the scene would
     * rather have the frame time than the detail, even though the detail is
     * still there to be drawn. That is an art decision, which is why it is
     * authored per scene and off by default.
     *
     * `range_fade` is how the cut arrives. A hard threshold on view distance
     * would sweep a visible line across the ground as the camera moves, so
     * the layer instead fades over the last `range_fade` of its own range: at
     * 0.25 a layer with a max_range of 80 is at full strength to 60, gone at
     * 80, and interpolated between. It is a fraction rather than a width in
     * world units because a band of fixed world width covers fewer and fewer
     * pixels the further out it sits, and a fade the eye can see the near end
     * of but not the far end of reads as a hard edge again. One fraction is
     * shared by all three layers: the distances differ because the layers
     * differ, but how gently a cut arrives is a property of the camera. */
    float range_fade; /* 0..1 fraction of each layer's max_range over which
                       * that layer fades out; clamped away from 0 so the
                       * cut always has a band to fade over */
    FlecsSurfaceDust dust;
    FlecsSurfaceGrime grime;
    FlecsSurfaceScratch scratch;
    FlecsSurfaceGrain grain;
    FlecsSurfaceStreak streak;
    FlecsSurfaceEdge edge;
    FlecsSurfaceBurn burn;
    FlecsSurfaceWetness wetness;
});

/* Animated cloud-shadow illusion, for one view. A drifting tiling noise field
 * in world XZ attenuates direct sunlight, the same way the volumetric cloud
 * system's baked shadow texture does, but without needing the cloud system:
 * the two combine with a min() when both are active.
 *
 * Carried by value in FlecsRenderView.cloud_shadow, and opt-in the way the
 * seven surface layers are: `enabled` defaults to false and strength to 0, so
 * a view that says nothing about it - or that had its cloud_shadow block
 * deleted - gets no cloud shadow at all. The rest of the defaults are a usable
 * field, so switching the layer on means one flag and one number. */
ECS_STRUCT(flecs_engine_cloud_shadow_t, {
    ecs_bool_t enabled; /* the default, false, skips both noise taps in the
                         * shadow lookup */
    float strength;   /* 0..1 how dark a fully clouded spot gets; 0 disables */
    float scale;      /* world units per noise cell */
    float coverage;   /* 0..1 fraction of the ground under cloud */
    float softness;   /* 0..1 shadow edge width */
    float speed;      /* drift speed in world units per second */
    float direction;  /* drift heading in radians around world Y */
});

/* Fill both blocks with the defaults documented above. Called from the
 * FlecsRenderView constructor, so every view starts out with no surface
 * effects and no cloud shadow, and renders whichever of them its scene goes
 * on to switch on. */
void flecsEngine_surfaceEffects_defaults(
    flecs_engine_surface_effects_t *fx,
    flecs_engine_cloud_shadow_t *cloud);

/* Wetness has two states, not one, and the difference between them is what a
 * wet street actually looks like: most of it is *damp* - dark, porous, matte,
 * because water soaked into the asphalt - and a fraction of it is under
 * standing water, which is a mirror. Only the second one reflects. A layer
 * that made the whole street reflective is a layer that turned the street
 * into a lake; `coverage` is the knob that says how much of it is lake.
 *
 * The pool field itself is static in world space: a fixed fbm noise field,
 * sampled by world XZ, cut at a threshold. `coverage` moves the cut, it does
 * not re-seed the field, so the puddle that appeared first is still in the
 * same place when the street is soaked and has simply grown into its
 * neighbours. That is what makes a downpour read as the same street getting
 * more water rather than as a new pattern every frame.
 *
 * `coverage` is a real fraction of up-facing area, not a threshold dressed up
 * as one: the engine keeps the noise field's inverse CDF and turns the
 * fraction the scene asked for into the cut that produces it, so 0.1 covers a
 * tenth of the ground and 0.5 covers half. 0 is a street with no standing
 * water anywhere and 1 is a continuous sheet.
 *
 * This component says how wet the world is; it does not say that anyone draws
 * it. A view renders wetness only where its own
 * RenderView.surface_effects.wetness.enabled is true and the master
 * surface_effects.enabled above it is, the same opt-in the seven wear layers
 * take - so setting this component on a world with no view asking for it is a
 * scene that got rained on with the water switched off. */
ECS_STRUCT(FlecsWetness, {
    float amount;      /* 0..1 master wetness. 0 is bone dry and skips the
                        * whole pass; 1 is a surface still running with
                        * water. Everything else here scales by it.
                        *
                        * This is the whole scene's wetness in one number, and
                        * it is what a game writes when it wants to say how
                        * wet the world is and be done: setting it fills both
                        * reservoirs below with the same value, which is
                        * exactly the behaviour every scene had before there
                        * were two of them. FlecsWeather keeps integrating it
                        * alongside the reservoirs, so a game that reads
                        * `amount` to drive rain particles or a mood still
                        * reads what it always did. */
    float coverage;    /* 0..1 fraction of the up-facing ground that is under
                        * a reflective sheet of water. Calibrated against the
                        * pool noise field, so what the scene writes is what
                        * gets measured: at 0.25 a quarter of the ground is
                        * pool and three quarters is damp asphalt. 0 means no
                        * pools at all - the ground still darkens, it just
                        * never mirrors - and 1 is one unbroken sheet.
                        *
                        * Scaled down by `amount`, so a street that is drying
                        * loses its pools before it loses its damp: the
                        * effective coverage is coverage * smoothstep(0, 0.5,
                        * amount), which is 0 at bone dry, half the authored
                        * coverage at amount 0.25 and the full authored
                        * coverage from amount 0.5 up. */
    float puddle_level; /* Deprecated alias of `coverage`, kept so scenes
                         * written against the first version of this component
                         * still load. It used to be a threshold on the pool
                         * noise rather than a fraction; anything above 0 is
                         * now read as a coverage fraction and overrides
                         * `coverage`. 0, the default, means "not used". Set
                         * `coverage` in new scenes. */
    float ripple;      /* 0..1 rain ripple strength inside puddles. Meant to
                        * track how hard it is raining right now, which is
                        * what FlecsWeather sets it to: rings stop arriving
                        * the moment the rain does, while the water they
                        * landed in stays until it dries. 0 skips the ripple
                        * taps outright. */
    float ripple_scale; /* world units per ripple cell; roughly the spacing
                         * between raindrop impacts. 0 means the default. */
    float darkening;   /* 0..1 how far a fully wet surface darkens, before
                        * porosity. A polished or metallic surface has no
                        * pores for the water to fill, so it barely darkens
                        * however wet it is; the shader works porosity out
                        * from the material's own metallic and roughness
                        * rather than asking the scene to author it twice. */
    float roughness_wet; /* roughness floor a fully wet surface reaches. The
                          * film only ever smooths: a surface already glossier
                          * than this keeps its own roughness, so wetting a
                          * mirror does not roughen it. Reached inside pools
                          * and inside drip runs; damp ground only travels
                          * `damp_gloss` of the way here. */
    float damp_gloss;  /* 0..1 how far the damp ground *outside* the pools is
                        * pulled toward roughness_wet. Damp asphalt is dark,
                        * not shiny: water that soaked into a porous surface
                        * has no flat film on top to reflect with, so the
                        * default of 0.15 leaves a roughness 0.72 street at
                        * about 0.62 - well above a typical SSR
                        * roughness_cutoff, so screen space reflections fire
                        * in the pools and nowhere else. 1 reproduces the
                        * behaviour from before pools had a coverage: the
                        * whole surface goes to roughness_wet. */
    float puddle_roughness; /* roughness inside a puddle. Standing water is
                             * flatter than a wet surface, which is what makes
                             * a puddle mirror a sign that the asphalt around
                             * it only smears. */
    float drip;        /* 0..1 strength of the drip runs on vertical faces.
                        * Water does not pool on a wall, it runs down it, so
                        * the same wetness reads as vertical streaks there -
                        * darker and glossier than the wall around them. */
    float water_fast;  /* 0..1 the draining reservoir: the water left on a
                        * surface that lets it through. It answers rain
                        * quickly and it is gone quickly, which is what sand,
                        * soil and gravel do - dark while it is raining, dry
                        * again a few minutes after it stops, and never a
                        * puddle. Integrated by FlecsWeather. */
    float water_slow;  /* 0..1 the standing reservoir: the water on a surface
                        * that has nowhere to put it. It takes a while to
                        * build and a long while to leave, which is what
                        * asphalt, concrete and paving do - still mirroring
                        * long after the rain stopped. Integrated by
                        * FlecsWeather.
                        *
                        * A material picks between the two with
                        * FlecsSurfaceEffects.absorption: 0 follows water_slow,
                        * 1 follows water_fast, and the shader mixes. Leave
                        * both at 0 and write `amount` instead to drive the
                        * whole scene by hand: with no FlecsWeather nothing
                        * integrates them, and both reservoirs are read as
                        * `amount`, which is what every scene written before
                        * absorption existed gets. */
});

extern ECS_COMPONENT_DECLARE(FlecsWetness);

ECS_STRUCT(FlecsWeather, {
    float rain;        /* 0..1 how hard it is raining right now. This is the
                        * one value a day/night or storm system animates. */
    float wind_x;      /* wind velocity in world units per second. Not read by
                        * the wetness shader; it is here so rain particles,
                        * cloth and a game's own systems read the weather from
                        * one place instead of three. */
    float wind_z;
    float wet_up_rate; /* wetness gained per second at rain 1 */
    float dry_rate;    /* wetness lost per second at rain 0 */

    /* The two reservoirs in FlecsWetness are the same integration run at two
     * speeds, and these are the speeds. Each is in wetness per second and
     * each defaults to 0, which means "work it out from wet_up_rate and
     * dry_rate" - the fast one at 2.5x up and 4x down, the slow one at 0.7x
     * up and 0.35x down. A scene that only sets wet_up_rate/dry_rate, as
     * every scene did before there were two reservoirs, therefore still says
     * how fast its weather moves and gets the spread for free; a scene that
     * wants a specific pair of time constants writes them here. */
    float fast_up;     /* draining reservoir, gained per second at rain 1 */
    float fast_down;   /* draining reservoir, lost per second at rain 0 */
    float slow_up;     /* standing reservoir, gained per second at rain 1 */
    float slow_down;   /* standing reservoir, lost per second at rain 0 */

    /* Rain does not only wet the ground, it also thickens the air and greys
     * the sky, and both of those are the same number animated twice. These
     * two are the *authored* values and both default to -1, which means "work
     * it out from `rain`". Anything from 0 up is an explicit setting and wins
     * over the rain: a scene can have fog on a dry day (fog 0.7, rain 0) or
     * rain that leaves the air clear (fog 0, rain 1). */
    float fog;         /* 0..1 how much fog/haze the air carries. -1 derives
                        * it from rain * fog_from_rain. */
    float overcast;    /* 0..1 how overcast the sky reads. -1 derives it from
                        * rain * overcast_from_rain. */

    float fog_from_rain;      /* fog per unit of rain when `fog` is -1 */
    float overcast_from_rain; /* overcast per unit of rain when `overcast`
                               * is -1 */

    /* Weather that snaps is weather that pops: a downpour that starts on one
     * frame must not take the sky from blue to slate between two of them. The
     * two amounts below chase their targets with an exponential approach at
     * this rate, in units per second of the remaining distance. 0 means no
     * smoothing at all, which is what a scene wants when it is posing a
     * single frame rather than playing a storm. */
    float response;

    /* Derived, smoothed outputs. Written by the weather system every frame
     * and read by everything downstream: FlecsWeatherFog turns the first into
     * a fog density and FlecsOvercast turns the second into a sky. A scene
     * that wants to drive something of its own from the weather reads these
     * rather than re-deriving them from `rain`.
     *
     * Both start at -1, which is the "never integrated" marker: the first
     * update snaps them onto their target instead of easing toward it from
     * zero, so a scene whose first frame is the frame that gets captured sees
     * the weather it authored and not the weather it was fading out of. */
    float fog_amount;
    float overcast_amount;
});

extern ECS_COMPONENT_DECLARE(FlecsWeather);

/* Hands a height fog effect's thickness to the weather. Put it on the same
 * entity as FlecsHeightFog: while it is there the weather owns `density` and
 * `max_opacity`, interpolating each between the pair below by
 * FlecsWeather.fog_amount, and HeightFog's own two fields are only what the
 * effect falls back to when there is no FlecsWeather in the world. Take the
 * component off and the fog is authored by hand again.
 *
 * Everything else about the fog - falloff, base height, colour, the
 * inscatter split and, crucially, the `atmosphere` entity that makes fog fade
 * to the real sky colour rather than to a flat grey - stays where it was
 * authored. The weather says how much fog there is, not what it looks like. */
ECS_STRUCT(FlecsWeatherFog, {
    float clear_density;     /* HeightFog.density at fog 0 */
    float rain_density;      /* HeightFog.density at fog 1 */
    float clear_max_opacity; /* HeightFog.max_opacity at fog 0 */
    float rain_max_opacity;  /* HeightFog.max_opacity at fog 1 */
});

extern ECS_COMPONENT_DECLARE(FlecsWeatherFog);

/* Rain out of a clear blue sky with the sun still in it is the one thing an
 * overcast day never looks like. This component, put on the same entity as
 * FlecsAtmosphere, derives that sky from the weather: dense near-isotropic
 * aerosol washes the blue out to white, haze absorption turns the white to
 * slate, the sun disc dissolves, the direct sun flattens and the ambient
 * share rises so the shadows lift. It is one continuous mapping, so drizzle
 * is a slightly grey sky and a downpour is a heavy one, and it moves at
 * FlecsWeather.response so nothing pops when the rain starts.
 *
 * Precedence, in order:
 *
 * 1. No FlecsOvercast on the atmosphere entity means no overcast at all. A
 *    scene that never asked for it renders exactly as it did before this
 *    component existed.
 * 2. `amount` >= 0 is an explicit authored overcast and overrides the weather
 *    outright - and is applied as written, without smoothing, because an
 *    authored value is a statement rather than a target.
 * 3. `amount` < 0, the default, follows FlecsWeather.overcast_amount, which
 *    is itself either an authored FlecsWeather.overcast or rain-derived.
 * 4. The four strengths below scale one target each. Setting one to 0 hands
 *    that target back to the authored value even at full overcast, which is
 *    how a scene keeps, say, its own sun intensity while still greying its
 *    sky.
 *
 * Nothing here is ever written back into FlecsAtmosphere, FlecsDirectionalLight
 * or FlecsRenderView: the overcast is applied where those values are read for
 * a frame, so the authored sky survives underneath it, comes back unchanged
 * when the overcast lifts, and does not fight FlecsTimeOfDay over the sun. */
ECS_STRUCT(FlecsOvercast, {
    float amount;        /* 0..1 explicit overcast; -1 follows the weather */
    float sky_grey;      /* 0..1 strength of the sky desaturation: the Mie
                          * boost, the Rayleigh cut and the haze absorption
                          * together, which is the dome's colour */
    float sun_disk_fade; /* 0..1 how far the sun disc is gone at overcast 1 */
    float sun_dim;       /* 0..1 fraction of the direct sun removed at
                          * overcast 1 */
    float ambient_gain;  /* extra ambient at overcast 1, as a multiple of the
                          * view's authored ambient_intensity: 2 doubles it */
    float shadow_fade;   /* 0..1 fraction of the directional shadow removed at
                          * overcast 1. At 1 (the default) a fully overcast sky
                          * casts no directional shadow at all, which is what a
                          * sky with no sun in it does, and the cascade passes
                          * are skipped for the frame. */
});

extern ECS_COMPONENT_DECLARE(FlecsOvercast);

/* The anisotropy a fully overcast sky's Mie phase function is pulled toward.
 * A clear sky scatters hard forward (0.8); cloud droplets are far bigger and
 * far more numerous, and what reaches the eye is multiply scattered, so the
 * dome reads nearly uniform. */
#define FLECS_ENGINE_OVERCAST_ANISOTROPY (0.35f)

/* The overcast resolved into the multipliers its four consumers apply. Every
 * field is identity at overcast 0, which is what makes a scene with no
 * overcast render byte for byte as it did before. */
typedef struct {
    float amount;           /* the resolved 0..1 overcast itself */
    float rayleigh_scale;   /* multiplies Atmosphere.rayleigh.scattering_scale */
    float mie_scale;        /* multiplies Atmosphere.mie.scattering_scale */
    float mie_height_scale; /* multiplies Atmosphere.mie.scale_height_km */
    float mie_isotropy;     /* 0..1 blend of Atmosphere.mie.anisotropy toward
                             * FLECS_ENGINE_OVERCAST_ANISOTROPY */
    float haze_add;         /* added to Atmosphere.haze_absorption */
    float sun_disk_scale;   /* multiplies Atmosphere.sun_disk_intensity */
    float sun_light_scale;  /* multiplies the main directional light */
    float ambient_scale;    /* multiplies RenderView.ambient_intensity */
    float shadow_scale;     /* 1 = shadows as authored, 0 = no shadow at all
                             * (and no cascade passes rendered) */
    float sky_desaturation; /* 0 = the sky keeps its own colour, 1 = every
                             * direction of the dome is pulled all the way to
                             * its own luminance. Thinning the Rayleigh term
                             * alone leaves the anti-solar half of the sky
                             * blue, so the hue that survives it is taken out
                             * here, where every consumer of the sky (dome,
                             * aerial perspective, ambient probe, fog) reads
                             * the same greyed radiance. */
} flecs_engine_overcast_params_t;

void flecsEngine_weather_overcastParams(
    float overcast,
    const FlecsOvercast *cfg,
    flecs_engine_overcast_params_t *out);

float flecsEngine_weather_fogTarget(
    const FlecsWeather *weather);

float flecsEngine_weather_overcastTarget(
    const FlecsWeather *weather);

float flecsEngine_weather_approach(
    float current,
    float target,
    float rate,
    float dt);

float flecsEngine_weather_fogDensity(
    const FlecsWeatherFog *cfg,
    float fog_amount);

float flecsEngine_weather_fogMaxOpacity(
    const FlecsWeatherFog *cfg,
    float fog_amount);

/* Resolve the overcast an atmosphere entity renders under. Returns false -
 * and leaves `out` untouched - when the entity carries no FlecsOvercast or
 * when the resolved overcast is zero, so every caller can skip the whole
 * thing with one branch. */
bool flecsEngine_weather_atmosphereOvercast(
    const ecs_world_t *world,
    ecs_entity_t atmosphere,
    flecs_engine_overcast_params_t *out);

#endif
