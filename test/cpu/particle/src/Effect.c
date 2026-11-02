/**
 * @file Effect.c
 * @brief Tests for box_particle_burst_at: the pool of instances of a template, layers, scale,
 * prefab and script templates, the budgets of ParticleSettings and the effect counters.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

/** Frame delta of the tests, a fixed clock of a whole number of microseconds */
#define DELTA (0.1f)

/** Templates of the tests: Spark is one emitter at its root that bursts 5 particles living 0.3
 * seconds, Layered three emitter children whose last particles die at 0.3, 0.5 and 0.7
 * seconds, Glow a second one emitter template and Empty a prefab without emitter */
#define TEMPLATES \
    "using box.particle\n" \
    "using box.transform\n" \
    "@tree ChildOf\n" \
    "prefab Spark {\n" \
    "  ParticleEmitter: {max: 100, lifetime: {0.3, 0.3}, speed: {2, 2}, size: {0.5, 0.5}, " \
    "shape: EmitterSphere, radius: 1}\n" \
    "  ParticleBurst: {count: 5}\n" \
    "}\n" \
    "@tree ChildOf\n" \
    "prefab Layered {\n" \
    "  flash {\n" \
    "    Position: {1, 0, 0}\n" \
    "    ParticleEmitter: {lifetime: {0.2, 0.2}}\n" \
    "    ParticleBurst: {count: 4}\n" \
    "  }\n" \
    "  smoke {\n" \
    "    Position: {0, 2, 0}\n" \
    "    ParticleEmitter: {rate: 50, lifetime: {0.5, 0.5}}\n" \
    "    ParticleBurst: {count: 2, at: 0.2}\n" \
    "  }\n" \
    "  debris {\n" \
    "    ParticleEmitter: {lifetime: {0.3, 0.3}}\n" \
    "    ParticleBurst: {count: 3, at: 0.1, repeat: 1}\n" \
    "  }\n" \
    "}\n" \
    "@tree ChildOf\n" \
    "prefab Glow {\n" \
    "  ParticleEmitter: {lifetime: {0.3, 0.3}}\n" \
    "  ParticleBurst: {count: 2}\n" \
    "}\n" \
    "@tree ChildOf\n" \
    "prefab Empty {\n" \
    "  child { Position: {1, 0, 0} }\n" \
    "}\n"

/** Create a world that renders into a null backend, with the templates of the tests.
 *
 * @param null Receives the backend, freed by the caller after the world
 * @return The world
 */
static ecs_world_t* world_new(
    box_render_null_t **null)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);
    test_assert(ecs_script_run(world, "templates", TEMPLATES, NULL) == 0);

    return world;
}

/** Look up a template of the tests.
 *
 * @param world The world
 * @param name Its name
 * @return The template
 */
static ecs_entity_t template_of(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t result = ecs_lookup(world, name);
    test_assert(result != 0);

    return result;
}

/** Play a template at a point.
 *
 * @param world The world
 * @param name Name of the template
 * @param x X of the point
 * @param y Y of the point
 * @param z Z of the point
 * @param scale Scale of the play
 * @return The root of the instance, 0 for none
 */
static ecs_entity_t play(
    ecs_world_t *world,
    const char *name,
    float x,
    float y,
    float z,
    float scale)
{
    vec3 position = { x, y, z };

    return box_particle_burst_at(world, template_of(world, name), position, scale);
}

/** Set the budgets of the effects.
 *
 * @param world The world
 * @param settings The budgets
 */
static void settings_set(
    ecs_world_t *world,
    ParticleSettings settings)
{
    ecs_singleton_set_ptr(world, ParticleSettings, &settings);
}

/** Run a frame and get the stats it wrote.
 *
 * @param world The world
 * @return The stats
 */
static ParticleStats frame(
    ecs_world_t *world)
{
    ecs_progress(world, DELTA);
    box_render_sync(world);
    const ParticleStats *stats = ecs_singleton_get(world, ParticleStats);
    test_assert(stats != NULL);

    return *stats;
}

/** Run frames.
 *
 * @param world The world
 * @param count Frames to run
 * @return The stats of the last one
 */
static ParticleStats frames(
    ecs_world_t *world,
    int32_t count)
{
    ParticleStats result = {0};
    int32_t i;
    for (i = 0; i < count; i ++) {
        result = frame(world);
    }

    return result;
}

/** Find the pool of an emitter in the last packet.
 *
 * @param null The backend
 * @param e The emitter
 * @return The pool, NULL when the packet has none
 */
static const box_render_particle_pool_t* pool_of(
    box_render_null_t *null,
    ecs_entity_t e)
{
    const box_render_particle_pool_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.particles); i ++) {
        const box_render_particle_pool_t *p = ecs_vec_get_t(
            &null->last.particles, box_render_particle_pool_t, i);
        if (p->entity == e) {
            result = p;
        }
    }

    return result;
}

/** Look up a layer of an instance.
 *
 * @param world The world
 * @param root The root of the instance
 * @param name Name of the layer
 * @return The layer
 */
static ecs_entity_t layer_of(
    ecs_world_t *world,
    ecs_entity_t root,
    const char *name)
{
    ecs_entity_t result = ecs_lookup_child(world, root, name);
    test_assert(result != 0);

    return result;
}

/** Get the ParticleEffect of the root of an instance.
 *
 * @param world The world
 * @param root The root
 * @return The play
 */
static ParticleEffect effect_of(
    ecs_world_t *world,
    ecs_entity_t root)
{
    const ParticleEffect *result = ecs_get(world, root, ParticleEffect);
    test_assert(result != NULL);

    return *result;
}

/** Count the instances of a template, playing or not.
 *
 * @param world The world
 * @param name Name of the template
 * @return The instances
 */
static int32_t instances_of(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t t = template_of(world, name);
    int32_t result = 0;
    ecs_iter_t it = ecs_each_pair(world, EcsIsA, t);
    while (ecs_each_next(&it)) {
        result += it.count;
    }

    return result;
}

/** Get the length of a column of the model matrix of a pool.
 *
 * @param pool The pool
 * @param c The column
 * @return The length
 */
static float column_length(
    const box_render_particle_pool_t *pool,
    int32_t c)
{
    const float *m = pool->block.model[c];

    return sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
}

void Effect_reused_after_retire(void) {
    // particle/effect
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t first = play(world, "Spark", 0, 0, 0, 1);
    test_assert(first != 0);
    test_assert(ecs_has_pair(world, first, EcsIsA, template_of(world, "Spark")));
    ParticleStats stats = frame(world);
    test_int(stats.spawned, 5);
    test_int(stats.effects, 1);

    stats = frames(world, 3);
    test_int(stats.particles, 0);
    test_int(stats.effects, 0);
    test_bool(ecs_has_id(world, first, EcsDisabled), true);

    ecs_entity_t again = play(world, "Spark", 4, 0, 0, 1);
    test_assert(again == first);
    test_bool(ecs_has_id(world, first, EcsDisabled), false);
    test_int(instances_of(world, "Spark"), 1);
    stats = frame(world);
    test_int(stats.spawned, 5);
    test_int(stats.effects, 1);
    const box_render_particle_pool_t *pool = pool_of(null, first);
    test_assert(pool != NULL);
    test_flt(pool->block.model[3][0], 4);
    test_flt(effect_of(world, first).time, 0.1f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Effect_not_reused_before_retire(void) {
    // particle/effect
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t first = play(world, "Spark", 0, 0, 0, 1);
    frames(world, 2);
    ecs_entity_t second = play(world, "Spark", 2, 0, 0, 1);
    test_assert(second != 0);
    test_assert(second != first);
    test_int(instances_of(world, "Spark"), 2);

    ParticleStats stats = frame(world);
    test_int(stats.effects, 2);
    test_int(stats.particles, 10);
    stats = frame(world);
    test_int(stats.effects, 1);
    test_bool(ecs_has_id(world, first, EcsDisabled), true);
    test_bool(ecs_has_id(world, second, EcsDisabled), false);

    ecs_entity_t third = play(world, "Spark", 0, 0, 0, 1);
    test_assert(third == first);
    stats = frames(world, 2);
    test_int(stats.effects, 1);
    test_bool(ecs_has_id(world, second, EcsDisabled), true);
    test_int(instances_of(world, "Spark"), 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void Effect_layers_play_together(void) {
    // particle/effect
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t root = play(world, "Layered", 0, 0, 0, 1);
    test_assert(root != 0);
    ecs_entity_t flash = layer_of(world, root, "flash");
    ecs_entity_t smoke = layer_of(world, root, "smoke");
    ecs_entity_t debris = layer_of(world, root, "debris");

    ParticleStats stats = frame(world);
    test_int(stats.emitters, 3);
    test_int(stats.spawned, 4 + 5 + 3);
    test_uint(pool_of(null, flash)->block.count, 4);
    test_uint(pool_of(null, smoke)->block.count, 5);
    test_uint(pool_of(null, debris)->block.count, 3);

    ParticleEffect e = effect_of(world, root);
    test_assert(e.effect == template_of(world, "Layered"));
    test_flt(e.scale, 1);
    test_flt(e.time, 0.1f);
    test_flt(e.emit, 0.2f);
    test_flt(e.retire, 0.7f);

    stats = frame(world);
    test_int(stats.spawned, 5 + 2 + 3);
    stats = frame(world);
    test_int(stats.spawned, 0);
    test_int(stats.particles, 5 + 7 + 6);

    ecs_fini(world);
    box_render_null_free(null);
}

void Effect_layers_retire_together(void) {
    // particle/effect
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t root = play(world, "Layered", 0, 0, 0, 1);
    ecs_entity_t flash = layer_of(world, root, "flash");
    ecs_entity_t smoke = layer_of(world, root, "smoke");
    ecs_entity_t debris = layer_of(world, root, "debris");

    ParticleStats stats = frames(world, 5);
    test_int(stats.particles, 12);
    test_int(stats.effects, 1);
    test_bool(ecs_has_id(world, flash, EcsDisabled), false);
    test_bool(ecs_has_id(world, debris, EcsDisabled), false);

    stats = frame(world);
    test_int(stats.effects, 1);
    stats = frame(world);
    test_int(stats.particles, 0);
    test_int(stats.effects, 0);
    test_bool(ecs_has_id(world, root, EcsDisabled), true);
    test_bool(ecs_has_id(world, flash, EcsDisabled), true);
    test_bool(ecs_has_id(world, smoke, EcsDisabled), true);
    test_bool(ecs_has_id(world, debris, EcsDisabled), true);
    test_flt(effect_of(world, root).time, 0.7f);

    stats = frame(world);
    test_int(stats.emitters, 0);
    test_int(ecs_vec_count(&null->last.particles), 0);

    test_assert(play(world, "Layered", 0, 0, 0, 1) == root);
    stats = frame(world);
    test_int(stats.emitters, 3);
    test_int(stats.spawned, 4 + 5 + 3);
    test_bool(pool_of(null, smoke)->clear, true);

    ecs_fini(world);
    box_render_null_free(null);
}

void Effect_scale_sizes_and_speeds(void) {
    // particle/effect
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t root = play(world, "Spark", 0, 0, 0, 2);
    frame(world);
    const box_render_particle_pool_t *pool = pool_of(null, root);
    test_assert(pool != NULL);
    test_flt(pool->block.size[0], 1);
    test_flt(pool->block.size[1], 1);
    test_flt(pool->block.speed[0], 4);
    test_flt(pool->block.speed[1], 4);
    test_flt(effect_of(world, root).scale, 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void Effect_scale_volumes_and_offsets(void) {
    // particle/effect
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t spark = play(world, "Spark", 0, 0, 0, 3);
    ecs_entity_t root = play(world, "Layered", 10, 0, 0, 2);
    frame(world);
    const box_render_particle_pool_t *pool = pool_of(null, spark);
    test_assert(pool != NULL);
    test_flt(pool->block.extents[3], 1);
    test_flt(column_length(pool, 0), 3);
    test_flt(column_length(pool, 1), 3);
    test_flt(column_length(pool, 2), 3);

    pool = pool_of(null, layer_of(world, root, "flash"));
    test_assert(pool != NULL);
    test_flt(pool->block.model[3][0], 12);
    test_flt(pool->block.model[3][1], 0);
    pool = pool_of(null, layer_of(world, root, "smoke"));
    test_assert(pool != NULL);
    test_flt(pool->block.model[3][0], 10);
    test_flt(pool->block.model[3][1], 4);
    test_flt(column_length(pool, 0), 2);
    pool = pool_of(null, layer_of(world, root, "debris"));
    test_assert(pool != NULL);
    test_flt(pool->block.model[3][0], 10);
    test_flt(pool->block.model[3][1], 0);
    test_flt(column_length(pool, 0), 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void Effect_scale_zero_is_template_size(void) {
    // particle/effect
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t root = play(world, "Spark", 0, 0, 0, 0);
    ecs_entity_t other = play(world, "Spark", 0, 0, 0, -2);
    frame(world);
    const box_render_particle_pool_t *pool = pool_of(null, root);
    test_assert(pool != NULL);
    test_flt(pool->block.size[0], 0.5f);
    test_flt(pool->block.speed[0], 2);
    test_flt(column_length(pool, 0), 1);
    test_flt(effect_of(world, root).scale, 1);
    test_flt(pool_of(null, other)->block.size[0], 0.5f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Effect_prefab_template(void) {
    // particle/effect
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t root = play(world, "Glow", 1, 2, 3, 1);
    test_assert(root != 0);
    test_assert(ecs_has_pair(world, root, EcsIsA, template_of(world, "Glow")));
    ParticleStats stats = frame(world);
    test_int(stats.spawned, 2);
    const Position *p = ecs_get(world, root, Position);
    test_assert(p != NULL);
    test_flt(p->x, 1);
    test_flt(p->y, 2);
    test_flt(p->z, 3);

    ecs_fini(world);
    box_render_null_free(null);
}

void Effect_script_template(void) {
    // particle/effect
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    test_assert(ecs_script_run(world, "puff",
        "using box.particle\n"
        "template Puff {\n"
        "  ParticleEmitter: {lifetime: {0.2, 0.2}}\n"
        "  ParticleBurst: {count: 3}\n"
        "  ring {\n"
        "    ParticleEmitter: {lifetime: {0.4, 0.4}}\n"
        "    ParticleBurst: {count: 2}\n"
        "  }\n"
        "}\n", NULL) == 0);

    ecs_entity_t root = play(world, "Puff", 0, 0, 0, 1);
    test_assert(root != 0);
    ParticleStats stats = frame(world);
    test_int(stats.spawned, 5);
    test_int(stats.effects, 1);
    test_flt(effect_of(world, root).retire, 0.4f);

    stats = frames(world, 4);
    test_int(stats.particles, 0);
    test_int(stats.effects, 0);
    test_bool(ecs_has_id(world, root, EcsDisabled), true);

    test_assert(play(world, "Puff", 0, 0, 0, 1) == root);
    stats = frame(world);
    test_int(stats.spawned, 5);

    ecs_fini(world);
    box_render_null_free(null);
}

void Effect_without_emitter_returns_zero(void) {
    // particle/effect
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    vec3 origin = {0, 0, 0};

    test_assert(play(world, "Empty", 0, 0, 0, 1) == 0);
    test_assert(box_particle_burst_at(world, 0, origin, 1) == 0);
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, ParticleEmitter, {.max = 10});
    test_assert(box_particle_burst_at(world, plain, origin, 1) == 0);
    test_int(instances_of(world, "Empty"), 0);

    test_assert(ecs_script_run(world, "bare",
        "using box.transform\n"
        "template Bare {\n"
        "  child { Position: {1, 0, 0} }\n"
        "}\n", NULL) == 0);
    ecs_entity_t bare = ecs_lookup(world, "Bare");
    test_assert(bare != 0);
    test_assert(box_particle_burst_at(world, bare, origin, 1) == 0);
    test_assert(box_particle_burst_at(world, bare, origin, 1) == 0);

    ParticleStats stats = frame(world);
    test_int(stats.effects, 0);
    test_int(stats.dropped, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Effect_recycle_oldest(void) {
    // particle/effect
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    settings_set(world, (ParticleSettings){ .effect_max = 2 });

    ecs_entity_t first = play(world, "Spark", 0, 0, 0, 1);
    frame(world);
    ecs_entity_t second = play(world, "Spark", 1, 0, 0, 1);
    frame(world);
    ecs_entity_t third = play(world, "Spark", 2, 0, 0, 1);
    test_assert(third == first);
    test_assert(second != first);
    test_int(instances_of(world, "Spark"), 2);
    test_flt(effect_of(world, first).time, 0);

    ParticleStats stats = frame(world);
    test_int(stats.recycled, 1);
    test_int(stats.effects, 2);
    test_assert(pool_of(null, first) == NULL);
    stats = frame(world);
    test_int(stats.recycled, 0);
    test_uint(pool_of(null, first)->block.count, 5);
    test_flt(pool_of(null, first)->block.model[3][0], 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void Effect_recycle_total(void) {
    // particle/effect
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    settings_set(world, (ParticleSettings){ .effect_total = 2 });

    ecs_entity_t spark = play(world, "Spark", 0, 0, 0, 1);
    frame(world);
    ecs_entity_t glow = play(world, "Glow", 0, 0, 0, 1);
    frame(world);
    ecs_entity_t other = play(world, "Glow", 1, 0, 0, 1);
    test_assert(other != 0);
    test_assert(other != glow);
    test_assert(other != spark);
    test_bool(ecs_has_id(world, spark, EcsDisabled), true);
    test_bool(ecs_has_id(world, glow, EcsDisabled), false);

    ParticleStats stats = frame(world);
    test_int(stats.recycled, 1);
    test_int(stats.effects, 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void Effect_recycle_not_same_frame(void) {
    // particle/effect
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    settings_set(world, (ParticleSettings){ .effect_max = 1 });

    ecs_entity_t first = play(world, "Spark", 0, 0, 0, 1);
    test_assert(first != 0);
    test_assert(play(world, "Spark", 1, 0, 0, 1) == 0);
    ParticleStats stats = frame(world);
    test_int(stats.dropped, 1);
    test_int(stats.recycled, 0);
    test_int(stats.spawned, 5);

    test_assert(play(world, "Spark", 1, 0, 0, 1) == first);
    test_assert(play(world, "Spark", 2, 0, 0, 1) == 0);
    stats = frame(world);
    test_int(stats.recycled, 1);
    test_int(stats.dropped, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Create a camera at z 10 looking down -Z and a view of it.
 *
 * @param world The world
 */
static void view_new(
    ecs_world_t *world)
{
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 100});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
}

void Effect_skip_unseen(void) {
    // particle/effect
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    view_new(world);
    settings_set(world, (ParticleSettings){
        .effect_max = 1, .effect_overflow = EffectSkip });

    ecs_entity_t first = play(world, "Spark", 0, 0, 0, 1);
    frame(world);
    test_assert(play(world, "Spark", 0, 0, 20, 1) == 0);
    test_assert(play(world, "Spark", 500, 0, 0, 1) == 0);
    ParticleStats stats = frame(world);
    test_int(stats.dropped, 2);
    test_int(stats.recycled, 0);

    test_assert(play(world, "Spark", 1, 0, 0, 1) == first);
    stats = frame(world);
    test_int(stats.recycled, 1);
    test_int(stats.dropped, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Create a surface that renders into a texture.
 *
 * @param world The world
 * @param width Width in pixels
 * @param height Height in pixels
 * @param view_root Parent of the views of the surface, 0 for none
 * @return The surface
 */
static ecs_entity_t surface_new(
    ecs_world_t *world,
    int32_t width,
    int32_t height,
    ecs_entity_t view_root)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, Surface, {
        .width = width, .height = height, .texture = ecs_new(world), .view_root = view_root });

    return result;
}

void Effect_skip_view_surface(void) {
    // particle/effect
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    // A wide surface without view_root first, then a tall surface that claims the only view
    surface_new(world, 1000, 500, 0);
    ecs_entity_t root = ecs_new(world);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 100});
    ecs_entity_t view = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, view, View, {.camera = camera});
    surface_new(world, 500, 1000, root);
    settings_set(world, (ParticleSettings){
        .effect_max = 1, .effect_overflow = EffectSkip });

    ecs_entity_t first = play(world, "Spark", 0, 0, 0, 1);
    test_assert(first != 0);
    frame(world);

    // 10 in front of the camera the frustum is 5.77 high on each side: 2.89 wide at the
    // aspect 0.5 of the tall surface, 11.5 at the aspect 2 of the wide one. x 8 is outside.
    test_assert(play(world, "Spark", 8, 0, 0, 1) == 0);
    ParticleStats stats = frame(world);
    test_int(stats.dropped, 1);
    test_int(stats.recycled, 0);

    // x 2 is inside at either aspect
    test_assert(play(world, "Spark", 2, 0, 0, 1) == first);
    stats = frame(world);
    test_int(stats.recycled, 1);
    test_int(stats.dropped, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Effect_skip_distance(void) {
    // particle/effect
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    view_new(world);
    settings_set(world, (ParticleSettings){
        .effect_max = 1, .effect_overflow = EffectSkip, .effect_distance = 5 });

    ecs_entity_t first = play(world, "Spark", 0, 0, 0, 1);
    frame(world);
    test_assert(play(world, "Spark", 0, 0, 0, 1) == 0);
    test_assert(play(world, "Spark", 0, 0, 6, 1) == first);
    ParticleStats stats = frame(world);
    test_int(stats.dropped, 1);
    test_int(stats.recycled, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Effect_skip_without_view(void) {
    // particle/effect
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    settings_set(world, (ParticleSettings){
        .effect_total = 1, .effect_overflow = EffectSkip });

    test_assert(play(world, "Spark", 0, 0, 0, 1) != 0);
    frame(world);
    test_assert(play(world, "Glow", 0, 0, 0, 1) == 0);
    ParticleStats stats = frame(world);
    test_int(stats.dropped, 1);
    test_int(stats.effects, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Effect_merge_nearby(void) {
    // particle/effect
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    settings_set(world, (ParticleSettings){
        .effect_max = 1, .effect_overflow = EffectMerge, .effect_merge = 2 });

    ecs_entity_t first = play(world, "Spark", 0, 0, 0, 1);
    test_assert(play(world, "Spark", 1, 0, 0, 1) == first);
    ParticleStats stats = frame(world);
    test_int(stats.merged, 1);
    test_int(stats.spawned, 5);
    test_flt(pool_of(null, first)->block.model[3][0], 0);

    test_assert(play(world, "Spark", 0, 1.5f, 0, 1) == first);
    stats = frame(world);
    test_int(stats.merged, 1);
    test_int(stats.recycled, 0);
    test_flt(effect_of(world, first).time, 0.2f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Effect_merge_far_recycles(void) {
    // particle/effect
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    settings_set(world, (ParticleSettings){
        .effect_max = 1, .effect_overflow = EffectMerge, .effect_merge = 2 });

    ecs_entity_t first = play(world, "Spark", 0, 0, 0, 1);
    test_assert(play(world, "Spark", 10, 0, 0, 1) == 0);
    ParticleStats stats = frame(world);
    test_int(stats.merged, 0);
    test_int(stats.dropped, 1);

    test_assert(play(world, "Spark", 10, 0, 0, 1) == first);
    stats = frame(world);
    test_int(stats.recycled, 1);
    test_int(stats.merged, 0);
    const Position *p = ecs_get(world, first, Position);
    test_flt(p->x, 10);

    ecs_fini(world);
    box_render_null_free(null);
}

void Effect_stats(void) {
    // particle/effect
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ParticleStats stats = frame(world);
    test_int(stats.effects, 0);
    test_int(stats.recycled, 0);
    test_int(stats.merged, 0);
    test_int(stats.dropped, 0);

    play(world, "Spark", 0, 0, 0, 1);
    play(world, "Spark", 1, 0, 0, 1);
    play(world, "Layered", 2, 0, 0, 1);
    stats = frame(world);
    test_int(stats.effects, 3);
    test_int(stats.emitters, 5);

    settings_set(world, (ParticleSettings){
        .effect_max = 2, .effect_overflow = EffectMerge, .effect_merge = 1 });
    play(world, "Spark", 0, 0, 0, 1);
    play(world, "Spark", 50, 0, 0, 1);
    stats = frame(world);
    test_int(stats.effects, 3);
    test_int(stats.merged, 1);
    test_int(stats.recycled, 1);
    test_int(stats.dropped, 0);

    stats = frame(world);
    test_int(stats.merged, 0);
    test_int(stats.recycled, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Effect_delete_template(void) {
    // particle/effect
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t playing = play(world, "Layered", 0, 0, 0, 1);
    ecs_entity_t flash = layer_of(world, playing, "flash");
    ecs_entity_t spark = play(world, "Spark", 0, 0, 0, 1);
    frames(world, 4);
    test_bool(ecs_has_id(world, spark, EcsDisabled), true);

    ecs_delete(world, template_of(world, "Spark"));
    ecs_delete(world, template_of(world, "Layered"));
    ParticleStats stats = frame(world);
    test_assert(!ecs_is_alive(world, spark));
    test_assert(!ecs_is_alive(world, playing));
    test_assert(!ecs_is_alive(world, flash));
    test_int(stats.effects, 0);

    stats = frame(world);
    test_int(stats.emitters, 0);
    test_int(stats.bytes, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Effect_script_function(void) {
    // particle/effect
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    test_assert(ecs_script_run(world, "play",
        "using box.particle\n"
        "const root: entity = box.particle.burst_at(Spark, {1, 2, 3}, 2)\n", NULL) == 0);
    ParticleStats stats = frame(world);
    test_int(stats.effects, 1);
    test_int(stats.spawned, 5);
    test_int(instances_of(world, "Spark"), 1);

    ecs_iter_t it = ecs_each(world, ParticleEffect);
    test_bool(ecs_each_next(&it), true);
    test_int(it.count, 1);
    ecs_entity_t root = it.entities[0];
    ecs_iter_fini(&it);
    test_flt(effect_of(world, root).scale, 2);
    const Position *p = ecs_get(world, root, Position);
    test_flt(p->x, 1);
    test_flt(p->y, 2);
    test_flt(p->z, 3);

    test_assert(ecs_script_run(world, "layered",
        "const root: entity = box.particle.burst_at(Layered, {-4, 0, 0}, 1)\n", NULL) == 0);
    stats = frame(world);
    test_int(stats.effects, 2);
    test_int(instances_of(world, "Layered"), 1);
    it = ecs_each_pair(world, EcsIsA, template_of(world, "Layered"));
    test_bool(ecs_each_next(&it), true);
    ecs_entity_t layered = it.entities[0];
    ecs_iter_fini(&it);
    const box_render_particle_pool_t *pool = pool_of(null, layer_of(world, layered, "debris"));
    test_assert(pool != NULL);
    test_uint(pool->block.count, 3);
    test_flt(pool->block.model[3][0], -4);

    ecs_fini(world);
    box_render_null_free(null);
}
