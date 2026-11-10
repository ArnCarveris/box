/**
 * @file ShadowAtlas.c
 * @brief Tests for the shadow atlas of the spot and point lights: its settings, the tile
 * allocation and what the light extract sends for it.
 *
 * The allocation cases call box_render_shadow_allocate directly; the extract cases render into
 * the null backend with a camera at (0, 2, 10) looking down -Z and a spot light 4 units above
 * the origin shining straight down, or a point light there.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "bake.h"
#include "particles.h"

/** Check that the tiles of an allocation lie in the atlas, are powers of two and do not overlap.
 *
 * @param places The tiles
 * @param count Number of tiles, those without a size included
 * @param resolution Edge of the atlas
 */
static void tiles_check(
    const box_render_shadow_place_t *places,
    int32_t count,
    int32_t resolution)
{
    int32_t i, j;
    for (i = 0; i < count; i ++) {
        const box_render_shadow_place_t *a = &places[i];
        if (!a->size) {
            continue;
        }

        test_assert((a->size & (a->size - 1)) == 0);
        test_assert(a->x >= 0);
        test_assert(a->y >= 0);
        test_assert(a->x + a->size <= resolution);
        test_assert(a->y + a->size <= resolution);
        test_assert(a->x % a->size == 0);
        test_assert(a->y % a->size == 0);
        for (j = i + 1; j < count; j ++) {
            const box_render_shadow_place_t *b = &places[j];
            if (!b->size) {
                continue;
            }

            bool apart = a->x + a->size <= b->x || b->x + b->size <= a->x ||
                a->y + a->size <= b->y || b->y + b->size <= a->y;
            test_assert(apart);
        }
    }
}

/** Find the tile of a light in an allocation.
 *
 * @param requests The requests
 * @param places The tiles, in the order of the requests
 * @param count Number of requests
 * @param light The light
 * @return The tile of the light
 */
static box_render_shadow_place_t tile_of(
    const box_render_shadow_request_t *requests,
    const box_render_shadow_place_t *places,
    int32_t count,
    ecs_entity_t light)
{
    box_render_shadow_place_t result = {0};
    int32_t i;
    for (i = 0; i < count; i ++) {
        if (requests[i].light == light) {
            result = places[i];
        }
    }

    return result;
}

/* The allocation cases call no flecs function that would set up the OS API it allocates with. */
void ShadowAtlas_setup(void) {
    ecs_os_set_api_defaults();
}

void ShadowAtlas_script_defaults(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "atlas",
        "using box.light\n"
        "atlas { ShadowAtlas: {} }\n"
        "spot { SpotLight: {} }\n"
        "small { ShadowAtlas: {resolution: 512} }\n", NULL) == 0);

    const ShadowAtlas *atlas = ecs_get(world, ecs_lookup(world, "atlas"), ShadowAtlas);
    test_assert(atlas != NULL);
    test_int(atlas->resolution, 2048);
    test_int(atlas->min_tile, 64);
    atlas = ecs_get(world, ecs_lookup(world, "small"), ShadowAtlas);
    test_int(atlas->resolution, 512);
    test_int(atlas->min_tile, 64);
    const SpotLight *spot = ecs_get(world, ecs_lookup(world, "spot"), SpotLight);
    test_assert(spot != NULL);
    test_bool(spot->shadows, false);
    test_int(spot->shadow_resolution, 512);

    ecs_fini(world);
}

void ShadowAtlas_c_defaults(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ShadowAtlas);
    const ShadowAtlas *atlas = ecs_get(world, e, ShadowAtlas);
    test_int(atlas->resolution, 2048);
    test_int(atlas->min_tile, 64);

    ecs_fini(world);
}

void ShadowAtlas_settings_resolve(void) {
    // lighting/shadows
    ShadowAtlas out;
    box_render_shadow_atlas_settings(NULL, &out);
    test_int(out.resolution, 2048);
    test_int(out.min_tile, 64);
    box_render_shadow_atlas_settings(&(ShadowAtlas){3000, 100}, &out);
    test_int(out.resolution, 2048);
    test_int(out.min_tile, 64);
    box_render_shadow_atlas_settings(&(ShadowAtlas){100, 0}, &out);
    test_int(out.resolution, 256);
    test_int(out.min_tile, 1);
    box_render_shadow_atlas_settings(&(ShadowAtlas){100000, 99999}, &out);
    test_int(out.resolution, 8192);
    test_int(out.min_tile, 8192);

    ShadowAtlas atlas = {2048, 64};
    test_int(box_render_shadow_request_size(512, &atlas), 512);
    test_int(box_render_shadow_request_size(300, &atlas), 256);
    test_int(box_render_shadow_request_size(10, &atlas), 64);
    test_int(box_render_shadow_request_size(0, &atlas), 64);
    test_int(box_render_shadow_request_size(100000, &atlas), 2048);
}

void ShadowAtlas_every_light_fits(void) {
    // lighting/shadows
    ShadowAtlas atlas = {2048, 64};
    box_render_shadow_request_t requests[] = {
        {10, 512, 1}, {11, 1024, 2}, {12, 256, 0}, {13, 512, 3}, {14, 64, 5}
    };
    box_render_shadow_place_t places[5];
    int32_t dropped = -1, i;
    test_int(box_render_shadow_allocate(requests, 5, &atlas, places, &dropped), 5);
    test_int(dropped, 0);
    for (i = 0; i < 5; i ++) {
        test_int(places[i].size, requests[i].size);
    }

    tiles_check(places, 5, 2048);
}

void ShadowAtlas_over_budget_shrinks_then_drops(void) {
    // lighting/shadows
    ShadowAtlas atlas = {256, 32};
    box_render_shadow_request_t requests[] = {
        {1, 128, 0}, {2, 128, 1}, {3, 256, 2}, {4, 128, 3}, {5, 64, 4}
    };
    box_render_shadow_place_t places[5];
    int32_t dropped = 0;
    test_int(box_render_shadow_allocate(requests, 5, &atlas, places, &dropped), 4);
    test_int(dropped, 1);
    test_int(places[0].size, 128);
    test_int(places[1].size, 128);
    test_int(places[2].size, 128);
    test_int(places[3].size, 128);
    test_int(places[4].size, 0);
    tiles_check(places, 5, 256);
}

void ShadowAtlas_shrinks_to_min_tile(void) {
    // lighting/shadows
    ShadowAtlas atlas = {256, 64};
    box_render_shadow_request_t requests[] = {
        {1, 128, 0}, {2, 128, 1}, {3, 128, 2}, {4, 64, 3}, {5, 128, 4}, {6, 128, 5},
        {7, 128, 6}, {8, 128, 7}
    };
    box_render_shadow_place_t places[8];
    int32_t dropped = 0;
    test_int(box_render_shadow_allocate(requests, 8, &atlas, places, &dropped), 7);
    test_int(dropped, 1);
    test_int(places[0].size, 128);
    test_int(places[3].size, 64);
    test_int(places[4].size, 64);
    test_int(places[5].size, 64);
    test_int(places[6].size, 64);
    test_int(places[7].size, 0);
    tiles_check(places, 8, 256);
}

void ShadowAtlas_camera_move_keeps_placement(void) {
    // lighting/shadows
    ShadowAtlas atlas = {1024, 64};
    box_render_shadow_request_t near[] = {
        {21, 512, 0}, {22, 256, 1}, {23, 256, 2}, {24, 128, 3}
    };
    box_render_shadow_request_t far[] = {
        {21, 512, 3}, {22, 256, 2}, {23, 256, 1}, {24, 128, 0}
    };
    box_render_shadow_place_t a[4], b[4];
    int32_t i;
    test_int(box_render_shadow_allocate(near, 4, &atlas, a, NULL), 4);
    test_int(box_render_shadow_allocate(far, 4, &atlas, b, NULL), 4);
    for (i = 0; i < 4; i ++) {
        test_int(a[i].size, b[i].size);
        test_int(a[i].x, b[i].x);
        test_int(a[i].y, b[i].y);
    }

    tiles_check(a, 4, 1024);
}

void ShadowAtlas_same_in_any_input_order(void) {
    // lighting/shadows
    ShadowAtlas atlas = {512, 64};
    box_render_shadow_request_t ordered[] = {
        {31, 256, 0}, {32, 256, 1}, {33, 256, 2}, {34, 256, 3}, {35, 128, 3}, {36, 512, 9}
    };
    box_render_shadow_request_t shuffled[] = {
        {36, 512, 9}, {33, 256, 2}, {35, 128, 3}, {31, 256, 0}, {34, 256, 3}, {32, 256, 1}
    };
    box_render_shadow_place_t a[6], b[6];
    int32_t i, dropped_a = 0, dropped_b = 0;
    test_int(box_render_shadow_allocate(ordered, 6, &atlas, a, &dropped_a),
        box_render_shadow_allocate(shuffled, 6, &atlas, b, &dropped_b));
    test_int(dropped_a, dropped_b);
    for (i = 0; i < 6; i ++) {
        box_render_shadow_place_t other = tile_of(shuffled, b, 6, ordered[i].light);
        test_int(a[i].size, other.size);
        test_int(a[i].x, other.x);
        test_int(a[i].y, other.y);
    }

    tiles_check(a, 6, 512);
    tiles_check(b, 6, 512);
}

void ShadowAtlas_ties_by_size_then_entity(void) {
    // lighting/shadows
    ShadowAtlas atlas = {256, 64};
    box_render_shadow_request_t sizes[] = { {9, 128, 1}, {3, 256, 1}, {5, 128, 1} };
    box_render_shadow_place_t places[5];
    int32_t dropped = 0;
    test_int(box_render_shadow_allocate(sizes, 3, &atlas, places, &dropped), 1);
    test_int(dropped, 2);
    test_int(places[1].size, 256);
    test_int(places[0].size, 0);
    test_int(places[2].size, 0);

    box_render_shadow_request_t entities[] = {
        {5, 128, 1}, {4, 128, 1}, {3, 128, 1}, {2, 128, 1}, {1, 128, 1}
    };
    test_int(box_render_shadow_allocate(entities, 5, &atlas, places, &dropped), 4);
    test_int(dropped, 1);
    test_int(places[0].size, 0);
    test_int(places[1].size, 128);
    test_int(places[4].size, 128);
    test_int(places[4].x, 0);
    test_int(places[4].y, 0);
    tiles_check(places, 5, 256);
}

void ShadowAtlas_at_most_64_tiles(void) {
    // lighting/shadows
    ShadowAtlas atlas = {2048, 64};
    box_render_shadow_request_t requests[70];
    box_render_shadow_place_t places[70];
    int32_t i, dropped = 0;
    for (i = 0; i < 70; i ++) {
        requests[i] = (box_render_shadow_request_t){
            (ecs_entity_t)(100 + i), 64, (float)(69 - i)
        };
    }

    test_int(box_render_shadow_allocate(requests, 70, &atlas, places, &dropped),
        BOX_RENDER_SHADOW_TILES);
    test_int(dropped, 70 - BOX_RENDER_SHADOW_TILES);
    for (i = 0; i < 70; i ++) {
        test_int(places[i].size, i < 70 - BOX_RENDER_SHADOW_TILES ? 0 : 64);
    }

    tiles_check(places, 70, 2048);
}

/** Create a world that renders into the null backend, with a camera at (0, 2, 10) looking down
 * -Z and a view of it that renders shadows.
 *
 * @param null Receives the backend
 * @param view Receives the view
 * @return The world
 */
static ecs_world_t* extract_world(
    box_render_null_t **null,
    ecs_entity_t *view)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 2, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    *view = ecs_new(world);
    ecs_add(world, *view, View);
    View *v = ecs_get_mut(world, *view, View);
    v->camera = camera;
    v->shadows.enabled = true;
    ecs_modified(world, *view, View);
    ecs_singleton_set(world, Scene, {.punctual_shadows = true});

    return world;
}

/** Create a spot light shining straight down.
 *
 * @param world The world
 * @param position World position of the light
 * @param shadows Whether it casts shadows
 * @param mode Its bake mode
 * @return The light
 */
static ecs_entity_t spot_new(
    ecs_world_t *world,
    vec3 position,
    bool shadows,
    LightBakeMode mode)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {position[0], position[1], position[2]});
    ecs_set(world, e, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_add(world, e, SpotLight);
    SpotLight *s = ecs_get_mut(world, e, SpotLight);
    s->shadows = shadows;
    s->bake = mode;
    ecs_modified(world, e, SpotLight);

    return e;
}

/** Create a point light.
 *
 * @param world The world
 * @param position World position of the light
 * @param shadows Whether it casts shadows
 * @param mode Its bake mode
 * @return The light, with the default range of 10
 */
static ecs_entity_t point_new(
    ecs_world_t *world,
    vec3 position,
    bool shadows,
    LightBakeMode mode)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {position[0], position[1], position[2]});
    ecs_add(world, e, PointLight);
    PointLight *p = ecs_get_mut(world, e, PointLight);
    p->shadows = shadows;
    p->bake = mode;
    ecs_modified(world, e, PointLight);

    return e;
}

/** Find the tile of a light in a packet.
 *
 * @param p The packet
 * @param light The light
 * @return Its tile, NULL for none
 */
static const box_render_shadow_tile_t* extract_tile_of(
    const box_render_packet_t *p,
    ecs_entity_t light)
{
    const box_render_shadow_tile_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&p->shadow_tiles); i ++) {
        const box_render_shadow_tile_t *t = ecs_vec_get_t(
            &p->shadow_tiles, box_render_shadow_tile_t, i);
        if (t->light == light) {
            result = t;
        }
    }

    return result;
}

/** Run a frame into the null backend.
 *
 * @param world The world
 * @param null The backend
 * @return The packet the backend received
 */
static const box_render_packet_t* extract_frame(
    ecs_world_t *world,
    box_render_null_t *null)
{
    ecs_progress(world, 0);
    box_render_sync(world);

    return &null->last;
}

/** Get the tile record index the view list of the default scene holds for a light.
 *
 * @param world The world
 * @param light The light
 * @return The index, BOX_RENDER_SHADOW_NONE for none, 0xDEAD when the light is not listed
 */
static uint32_t extract_tile(
    ecs_world_t *world,
    ecs_entity_t light)
{
    box_bake_light_set_t set;
    uint32_t result = 0xDEAD;
    int32_t i;
    box_bake_view_lights(box_render_lights_state(world), 0, &set);
    for (i = 0; i < set.count; i ++) {
        if (set.entities[i] == light) {
            result = set.lights[i].shadow;
        }
    }

    return result;
}

/** Count the resource changes of a kind, an op and a handle in a packet.
 *
 * @param p The packet
 * @param kind Kind of object
 * @param op What happens to it
 * @param handle Handle of the object, 0 for any
 * @return The changes
 */
static int32_t extract_resources(
    const box_render_packet_t *p,
    box_render_resource_kind_t kind,
    box_render_resource_op_t op,
    uint32_t handle)
{
    int32_t i, result = 0;
    for (i = 0; i < ecs_vec_count(&p->resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(&p->resources, box_render_resource_t, i);
        result += r->kind == kind && r->op == op && (!handle || r->handle == handle);
    }

    return result;
}

/** Count the uploads into a buffer in a packet.
 *
 * @param p The packet
 * @param buffer Handle of the buffer
 * @return The uploads
 */
static int32_t extract_uploads(
    const box_render_packet_t *p,
    uint32_t buffer)
{
    int32_t i, result = 0;
    for (i = 0; i < ecs_vec_count(&p->uploads); i ++) {
        result += ecs_vec_get_t(&p->uploads, box_render_upload_t, i)->buffer == buffer;
    }

    return result;
}

void ShadowAtlas_extract_assigns_tile(void) {
    // lighting/shadows
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = extract_world(&null, &view);
    ecs_entity_t light = spot_new(world, (vec3){0, 4, 0}, true, LightBakeModeMixed);
    const box_render_packet_t *p = extract_frame(world, null);

    test_int(ecs_vec_count(&p->shadow_atlases), 1);
    test_int(ecs_vec_count(&p->shadow_tiles), 1);
    const box_render_shadow_atlas_t *atlas = ecs_vec_first(&p->shadow_atlases);
    const box_render_shadow_tile_t *tile = ecs_vec_first(&p->shadow_tiles);
    test_assert(atlas->handle >= BOX_RENDER_BUFFER_SHADOW_FIRST);
    test_assert(atlas->handle < BOX_RENDER_BUFFER_PARTICLE_FIRST ||
        atlas->handle > BOX_RENDER_BUFFER_PARTICLE_CURVES);
    test_int(atlas->resolution, 2048);
    test_int(atlas->first, 0);
    test_int(atlas->count, 1);
    test_uint(tile->light, light);
    test_int(tile->size, 512);
    test_int(extract_tile(world, light), 0);
    test_int(extract_resources(p, BOX_RENDER_SHADOW_ATLAS, BOX_RENDER_CREATE, atlas->handle), 1);
    test_int(extract_resources(p, BOX_RENDER_BUFFER, BOX_RENDER_CREATE, atlas->handle), 1);
    test_int(extract_uploads(p, atlas->handle), 1);

    const box_render_view_t *v = ecs_vec_first(&p->views);
    test_bool(v->shadows.spots, true);
    test_uint(v->shadows.atlas, atlas->handle);
    test_int(v->shadows.tiles, 1);
    test_int(v->shadows.filter_size, 3);
    test_bool(v->shadows.enabled, false);

    const RenderStatus *status = ecs_singleton_get(world, RenderStatus);
    test_int(status->shadow_tiles, 1);
    test_int(status->shadow_dropped, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void ShadowAtlas_extract_shadows_false_no_tile(void) {
    // lighting/shadows
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = extract_world(&null, &view);
    ecs_entity_t light = spot_new(world, (vec3){0, 4, 0}, false, LightBakeModeMixed);
    const box_render_packet_t *p = extract_frame(world, null);

    test_int(ecs_vec_count(&p->shadow_atlases), 0);
    test_int(extract_tile(world, light), BOX_RENDER_SHADOW_NONE);
    test_int(extract_resources(p, BOX_RENDER_SHADOW_ATLAS, BOX_RENDER_CREATE, 0), 0);
    test_bool(((const box_render_view_t*)ecs_vec_first(&p->views))->shadows.spots, false);
    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void ShadowAtlas_extract_view_shadows_disabled_no_tile(void) {
    // lighting/shadows
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = extract_world(&null, &view);
    View *v = ecs_get_mut(world, view, View);
    v->shadows.enabled = false;
    ecs_modified(world, view, View);
    ecs_entity_t light = spot_new(world, (vec3){0, 4, 0}, true, LightBakeModeMixed);
    const box_render_packet_t *p = extract_frame(world, null);

    test_int(ecs_vec_count(&p->shadow_atlases), 0);
    test_int(extract_tile(world, light), BOX_RENDER_SHADOW_NONE);
    test_int(ecs_singleton_get(world, RenderStatus)->shadow_dropped, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void ShadowAtlas_extract_scene_punctual_shadows_disabled_no_tile(void) {
    // lighting/shadows
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = extract_world(&null, &view);
    ecs_singleton_set(world, Scene, {.punctual_shadows = false});
    ecs_entity_t light = spot_new(world, (vec3){0, 4, 0}, true, LightBakeModeMixed);
    const box_render_packet_t *p = extract_frame(world, null);

    test_int(ecs_vec_count(&p->shadow_atlases), 0);
    test_int(extract_tile(world, light), BOX_RENDER_SHADOW_NONE);
    const box_render_view_t *rv = ecs_vec_first(&p->views);
    test_bool(rv->shadows.spots, false);

    ecs_fini(world);
    box_render_null_free(null);
}

void ShadowAtlas_extract_baked_light_no_tile(void) {
    // lighting/shadows
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = extract_world(&null, &view);
    ecs_singleton_add(world, LightBake);
    ecs_entity_t baked = spot_new(world, (vec3){-2, 4, 0}, true, LightBakeModeBaked);
    ecs_entity_t mixed = spot_new(world, (vec3){2, 4, 0}, true, LightBakeModeMixed);
    const box_render_packet_t *p = extract_frame(world, null);

    test_int(ecs_vec_count(&p->shadow_tiles), 1);
    test_uint(((const box_render_shadow_tile_t*)ecs_vec_first(&p->shadow_tiles))->light,
        mixed);
    test_int(extract_tile(world, baked), 0xDEAD);
    test_int(extract_tile(world, mixed), 0);
    test_int(ecs_singleton_get(world, RenderStatus)->shadow_dropped, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void ShadowAtlas_extract_unseen_light_no_tile(void) {
    // lighting/shadows
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = extract_world(&null, &view);
    ecs_entity_t behind = spot_new(world, (vec3){0, 2, 40}, true, LightBakeModeMixed);
    ecs_entity_t seen = spot_new(world, (vec3){0, 4, 0}, true, LightBakeModeMixed);
    const box_render_packet_t *p = extract_frame(world, null);

    test_int(ecs_vec_count(&p->shadow_tiles), 1);
    test_int(extract_tile(world, behind), BOX_RENDER_SHADOW_NONE);
    test_int(extract_tile(world, seen), 0);
    test_int(ecs_singleton_get(world, RenderStatus)->shadow_dropped, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void ShadowAtlas_extract_uploads_on_change(void) {
    // lighting/shadows
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = extract_world(&null, &view);
    ecs_entity_t light = spot_new(world, (vec3){0, 4, 0}, true, LightBakeModeMixed);
    const box_render_packet_t *p = extract_frame(world, null);
    uint32_t handle = ((const box_render_shadow_atlas_t*)ecs_vec_first(&p->shadow_atlases))->handle;
    uint32_t lights = ((const box_render_view_t*)ecs_vec_first(&p->views))->lights.buffer;
    test_int(extract_uploads(p, handle), 1);

    p = extract_frame(world, null);
    test_int(ecs_vec_count(&p->shadow_atlases), 1);
    test_int(extract_uploads(p, handle), 0);
    test_int(extract_uploads(p, lights), 0);
    test_int(extract_resources(p, BOX_RENDER_SHADOW_ATLAS, BOX_RENDER_CREATE, 0), 0);
    test_int(extract_resources(p, BOX_RENDER_BUFFER, BOX_RENDER_CREATE, handle), 0);

    ecs_set(world, light, Position, {1, 4, 0});
    p = extract_frame(world, null);
    test_int(extract_uploads(p, handle), 1);
    test_int(extract_resources(p, BOX_RENDER_SHADOW_ATLAS, BOX_RENDER_CREATE, 0), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void ShadowAtlas_extract_released_without_tiles(void) {
    // lighting/shadows
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = extract_world(&null, &view);
    ecs_entity_t light = spot_new(world, (vec3){0, 4, 0}, true, LightBakeModeMixed);
    const box_render_packet_t *p = extract_frame(world, null);
    uint32_t handle = ((const box_render_shadow_atlas_t*)ecs_vec_first(&p->shadow_atlases))->handle;

    SpotLight *s = ecs_get_mut(world, light, SpotLight);
    s->shadows = false;
    ecs_modified(world, light, SpotLight);
    p = extract_frame(world, null);
    test_int(ecs_vec_count(&p->shadow_atlases), 0);
    test_int(extract_resources(p, BOX_RENDER_SHADOW_ATLAS, BOX_RENDER_DESTROY, handle), 1);
    test_int(extract_resources(p, BOX_RENDER_BUFFER, BOX_RENDER_DESTROY, handle), 1);
    test_int(extract_tile(world, light), BOX_RENDER_SHADOW_NONE);

    p = extract_frame(world, null);
    test_int(extract_resources(p, BOX_RENDER_SHADOW_ATLAS, BOX_RENDER_DESTROY, 0), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void ShadowAtlas_extract_resolution_change(void) {
    // lighting/shadows
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = extract_world(&null, &view);
    spot_new(world, (vec3){0, 4, 0}, true, LightBakeModeMixed);
    const box_render_packet_t *p = extract_frame(world, null);
    uint32_t handle = ((const box_render_shadow_atlas_t*)ecs_vec_first(&p->shadow_atlases))->handle;

    ecs_singleton_set(world, ShadowAtlas, {.resolution = 1024, .min_tile = 64});
    p = extract_frame(world, null);
    test_int(extract_resources(p, BOX_RENDER_SHADOW_ATLAS, BOX_RENDER_CREATE, handle), 1);
    test_int(extract_resources(p, BOX_RENDER_BUFFER, BOX_RENDER_CREATE, handle), 0);
    const box_render_shadow_atlas_t *atlas = ecs_vec_first(&p->shadow_atlases);
    test_int(atlas->resolution, 1024);
    test_int(((const box_render_shadow_tile_t*)ecs_vec_first(&p->shadow_tiles))->size, 512);

    ecs_fini(world);
    box_render_null_free(null);
}

void ShadowAtlas_extract_full_atlas_drops(void) {
    // lighting/shadows
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = extract_world(&null, &view);
    ecs_singleton_set(world, ShadowAtlas, {.resolution = 512, .min_tile = 512});
    ecs_entity_t near = spot_new(world, (vec3){0, 4, 2}, true, LightBakeModeMixed);
    ecs_entity_t far = spot_new(world, (vec3){0, 4, -30}, true, LightBakeModeMixed);
    const box_render_packet_t *p = extract_frame(world, null);

    test_int(ecs_vec_count(&p->shadow_tiles), 1);
    test_int(extract_tile(world, near), 0);
    test_int(extract_tile(world, far), BOX_RENDER_SHADOW_NONE);
    const RenderStatus *status = ecs_singleton_get(world, RenderStatus);
    test_int(status->shadow_tiles, 1);
    test_int(status->shadow_dropped, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void ShadowAtlas_spot_projection(void) {
    // lighting/shadows
    box_render_shadow_record_t r;
    box_render_shadow_place_t place = {256, 512, 256};
    vec3 position = {1, 5, 2}, direction = {0, -1, 0};
    box_render_shadow_spot(position, direction, 10, 30, &place, 1024, &r);
    test_flt(r.params[0], 0.05f);
    test_flt(r.params[1], 10.0f);
    test_flt(r.params[3], 256.0f);
    test_flt(r.rect[0], 0.5f);
    test_flt(r.rect[1], 0.25f);
    test_flt(r.rect[2], 0.25f);
    test_flt(r.rect[3], 1.0f / 1024.0f);

    vec4 near = {1, 5 - 0.05f, 2, 1}, far = {1, -5, 2, 1}, mid = {1, 1, 2, 1}, out;
    glm_mat4_mulv(r.matrix, near, out);
    test_assert(fabsf(out[2] / out[3] - 1.0f) < 1e-4f);
    test_assert(fabsf(out[0]) < 1e-5f);
    test_assert(fabsf(out[1]) < 1e-5f);
    glm_mat4_mulv(r.matrix, far, out);
    test_assert(fabsf(out[2] / out[3]) < 1e-5f);
    glm_mat4_mulv(r.matrix, mid, out);
    float z = 4.0f;
    test_assert(fabsf(out[3] - z) < 1e-4f);
    test_assert(fabsf(out[2] / out[3] - 0.05f * (10.0f - z) / (z * (10.0f - 0.05f))) < 1e-5f);

    float tangent = r.params[2] * 256.0f * 0.5f;
    test_assert(tangent > tanf(glm_rad(30)));
    test_assert(tangent < tanf(glm_rad(35)));

    box_render_shadow_spot(position, direction, 10, 89, &place, 1024, &r);
    test_assert(fabsf(r.params[2] * 128.0f - tanf(glm_rad(BOX_RENDER_SHADOW_SPOT_ANGLE_MAX))) <
        1e-3f);
}

void ShadowAtlas_point_projection(void) {
    // lighting/shadows
    box_render_shadow_record_t r;
    box_render_shadow_place_t place = {256, 512, 256};
    vec3 position = {1, 5, 2};
    int32_t i, j;
    box_render_shadow_point(position, 10, &place, 1024, &r);
    test_flt(r.params[0], 0.05f);
    test_flt(r.params[1], 10.0f);
    test_assert(fabsf(r.params[2] - 2.0f * 2.4494897f / 256.0f) < 1e-6f);
    test_flt(r.params[3], 256.0f);
    test_flt(r.rect[0], 0.5f);
    test_flt(r.rect[1], 0.25f);
    test_flt(r.rect[2], 0.25f);
    test_flt(r.rect[3], 1.0f / 1024.0f);
    test_flt(r.light[0], 1.0f);
    test_flt(r.light[1], 5.0f);
    test_flt(r.light[2], 2.0f);
    test_flt(r.light[3], (float)BOX_RENDER_SHADOW_OCTAHEDRAL);
    for (i = 0; i < 4; i ++) {
        for (j = 0; j < 4; j ++) {
            test_flt(r.matrix[i][j], 0.0f);
        }
    }

    box_render_shadow_place_t spot = {256, 0, 0};
    box_render_shadow_spot(position, (vec3){0, -1, 0}, 10, 30, &spot, 1024, &r);
    test_flt(r.light[3], (float)BOX_RENDER_SHADOW_PERSPECTIVE);
    test_flt(r.light[1], 5.0f);

    test_flt(box_render_shadow_near(10), 0.05f);
    test_flt(box_render_shadow_far(10), 10.0f);
    test_flt(box_render_shadow_near(0.01f), BOX_RENDER_SHADOW_NEAR_MIN);
    test_flt(box_render_shadow_far(0.0001f), 2.0f * BOX_RENDER_SHADOW_NEAR_MIN);
}

/* The 6 planes keep the box around the sphere of the range and nothing beyond it */
void ShadowAtlas_point_cull_planes(void) {
    // lighting/shadows
    box_render_shadow_tile_t tile = {
        .projection = BOX_RENDER_SHADOW_OCTAHEDRAL,
        .position = {1, 5, 2},
        .near_ = 0.05f,
        .far_ = 10.0f
    };
    vec4 planes[6];
    vec3 inside[3] = {{1, 5, 2}, {10.5f, -4.5f, 11.5f}, {-8.5f, 14.5f, -7.5f}};
    vec3 outside[3] = {{11.5f, 5, 2}, {1, -5.5f, 2}, {1, 5, -8.5f}};
    int32_t i, k;
    box_render_shadow_point_planes(&tile, planes);
    for (i = 0; i < 6; i ++) {
        float len = sqrtf(planes[i][0] * planes[i][0] + planes[i][1] * planes[i][1] +
            planes[i][2] * planes[i][2]);
        test_assert(fabsf(len - 1.0f) < 1e-6f);
    }

    for (k = 0; k < 3; k ++) {
        bool in = true, out = false;
        for (i = 0; i < 6; i ++) {
            in = in && glm_vec3_dot(planes[i], inside[k]) + planes[i][3] >= 0;
            out = out || glm_vec3_dot(planes[i], outside[k]) + planes[i][3] < 0;
        }

        test_bool(in, true);
        test_bool(out, true);
    }
}

void ShadowAtlas_point_octant_planes(void) {
    box_render_shadow_tile_t tile = {
        .projection = BOX_RENDER_SHADOW_OCTAHEDRAL,
        .position = {1, 5, 2},
        .near_ = 0.05f,
        .far_ = 10.0f
    };
    vec4 planes[6];
    int32_t o, i;
    for (o = 0; o < BOX_RENDER_SHADOW_OCTANTS; o ++) {
        vec3 signs = {
            (o & 1) ? -1.0f : 1.0f, (o & 2) ? -1.0f : 1.0f, (o & 4) ? -1.0f : 1.0f
        };
        vec3 inside, opposite, beyond;
        bool in = true, at = true, out = false, far_ = false;
        for (i = 0; i < 3; i ++) {
            inside[i] = tile.position[i] + signs[i] * (float)(i + 1);
            opposite[i] = tile.position[i] - signs[i] * (float)(i + 1);
            beyond[i] = tile.position[i] + signs[i] * (i == 1 ? 10.5f : 1.0f);
        }

        box_render_shadow_octant_planes(&tile, o, planes);
        for (i = 0; i < 6; i ++) {
            float len = sqrtf(planes[i][0] * planes[i][0] + planes[i][1] * planes[i][1] +
                planes[i][2] * planes[i][2]);
            test_assert(fabsf(len - 1.0f) < 1e-6f);
            in = in && glm_vec3_dot(planes[i], inside) + planes[i][3] >= 0;
            at = at && glm_vec3_dot(planes[i], tile.position) + planes[i][3] >= -1e-5f;
            out = out || glm_vec3_dot(planes[i], opposite) + planes[i][3] < 0;
            far_ = far_ || glm_vec3_dot(planes[i], beyond) + planes[i][3] < 0;
        }

        test_bool(in, true);
        test_bool(at, true);
        test_bool(out, true);
        test_bool(far_, true);
    }
}

void ShadowAtlas_extract_point_assigns_tile(void) {
    // lighting/shadows
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = extract_world(&null, &view);
    ecs_entity_t light = point_new(world, (vec3){0, 4, 0}, true, LightBakeModeMixed);
    const box_render_packet_t *p = extract_frame(world, null);

    test_int(ecs_vec_count(&p->shadow_atlases), 1);
    test_int(ecs_vec_count(&p->shadow_tiles), 1);
    const box_render_shadow_atlas_t *atlas = ecs_vec_first(&p->shadow_atlases);
    const box_render_shadow_tile_t *tile = ecs_vec_first(&p->shadow_tiles);
    test_int(atlas->count, 1);
    test_uint(tile->light, light);
    test_int(tile->size, 512);
    test_int(tile->projection, BOX_RENDER_SHADOW_OCTAHEDRAL);
    test_flt(tile->position[0], 0.0f);
    test_flt(tile->position[1], 4.0f);
    test_flt(tile->position[2], 0.0f);
    test_flt(tile->near_, 0.05f);
    test_flt(tile->far_, 10.0f);
    test_int(extract_tile(world, light), 0);
    test_int(extract_uploads(p, atlas->handle), 1);

    const box_render_view_t *v = ecs_vec_first(&p->views);
    test_bool(v->shadows.spots, true);
    test_int(v->shadows.tiles, 1);
    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void ShadowAtlas_extract_point_shadows_false_no_tile(void) {
    // lighting/shadows
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = extract_world(&null, &view);
    ecs_entity_t light = point_new(world, (vec3){0, 4, 0}, false, LightBakeModeMixed);
    const box_render_packet_t *p = extract_frame(world, null);

    test_int(ecs_vec_count(&p->shadow_atlases), 0);
    test_int(extract_tile(world, light), BOX_RENDER_SHADOW_NONE);
    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void ShadowAtlas_extract_point_baked_no_tile(void) {
    // lighting/shadows
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = extract_world(&null, &view);
    ecs_singleton_add(world, LightBake);
    ecs_entity_t baked = point_new(world, (vec3){-2, 4, 0}, true, LightBakeModeBaked);
    ecs_entity_t mixed = point_new(world, (vec3){2, 4, 0}, true, LightBakeModeMixed);
    const box_render_packet_t *p = extract_frame(world, null);

    test_int(ecs_vec_count(&p->shadow_tiles), 1);
    test_uint(((const box_render_shadow_tile_t*)ecs_vec_first(&p->shadow_tiles))->light,
        mixed);
    test_int(extract_tile(world, baked), 0xDEAD);
    test_int(extract_tile(world, mixed), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

/* A spot and a point light are candidates of one allocation: the nearer one takes the only
 * tile, whichever kind it is, and both fit side by side when the atlas holds them */
void ShadowAtlas_extract_point_and_spot_share_budget(void) {
    // lighting/shadows
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = extract_world(&null, &view);
    ecs_singleton_set(world, ShadowAtlas, {.resolution = 512, .min_tile = 512});
    ecs_entity_t point = point_new(world, (vec3){0, 4, 2}, true, LightBakeModeMixed);
    ecs_entity_t spot = spot_new(world, (vec3){0, 4, -30}, true, LightBakeModeMixed);
    const box_render_packet_t *p = extract_frame(world, null);

    test_int(ecs_vec_count(&p->shadow_tiles), 1);
    test_int(extract_tile(world, point), 0);
    test_int(extract_tile(world, spot), BOX_RENDER_SHADOW_NONE);
    const RenderStatus *status = ecs_singleton_get(world, RenderStatus);
    test_int(status->shadow_tiles, 1);
    test_int(status->shadow_dropped, 1);

    ecs_set(world, point, Position, {0, 4, -30});
    ecs_set(world, spot, Position, {0, 4, 2});
    p = extract_frame(world, null);
    test_int(ecs_vec_count(&p->shadow_tiles), 1);
    test_int(extract_tile(world, spot), 0);
    test_int(extract_tile(world, point), BOX_RENDER_SHADOW_NONE);
    test_int(((const box_render_shadow_tile_t*)ecs_vec_first(&p->shadow_tiles))->projection,
        BOX_RENDER_SHADOW_PERSPECTIVE);
    status = ecs_singleton_get(world, RenderStatus);
    test_int(status->shadow_dropped, 1);

    ecs_singleton_set(world, ShadowAtlas, {.resolution = 1024, .min_tile = 64});
    p = extract_frame(world, null);
    test_int(ecs_vec_count(&p->shadow_tiles), 2);
    const box_render_shadow_tile_t *a = extract_tile_of(p, point);
    const box_render_shadow_tile_t *b = extract_tile_of(p, spot);
    test_assert(a != NULL);
    test_assert(b != NULL);
    test_int(a->projection, BOX_RENDER_SHADOW_OCTAHEDRAL);
    test_int(b->projection, BOX_RENDER_SHADOW_PERSPECTIVE);
    test_int(a->size, 512);
    test_int(b->size, 512);
    box_render_shadow_place_t places[2] = {
        {a->size, a->x, a->y}, {b->size, b->x, b->y}
    };
    tiles_check(places, 2, 1024);
    test_bool(extract_tile(world, point) != extract_tile(world, spot), true);
    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 2);
    test_int(ecs_singleton_get(world, RenderStatus)->shadow_dropped, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Make the tile of a spot light the way the light extract does.
 *
 * @param position World position of the light
 * @param direction Direction it shines in
 * @param range Its range
 * @param outer Its outer half angle in degrees
 * @return The tile, 256 texels at the origin of a 1024 atlas
 */
static box_render_shadow_tile_t spot_tile(
    vec3 position,
    vec3 direction,
    float range,
    float outer)
{
    box_render_shadow_record_t r;
    box_render_shadow_place_t place = {256, 0, 0};
    box_render_shadow_tile_t result = {
        .size = 256,
        .projection = BOX_RENDER_SHADOW_PERSPECTIVE
    };
    box_render_shadow_spot(position, direction, range, outer, &place, 1024, &r);
    glm_mat4_copy(r.matrix, result.matrix);
    glm_vec3_copy(position, result.position);
    result.near_ = r.params[0];
    result.far_ = r.params[1];

    return result;
}

/* The box of a spot light tile holds its cone: down to the range below the light, about
 * range * tan(30) to the side, nothing above the light */
void ShadowAtlas_spot_tile_bounds(void) {
    // lighting/shadows
    box_render_shadow_tile_t tile = spot_tile((vec3){1, 5, 2}, (vec3){0, -1, 0}, 10, 30);
    vec3 lo, hi;
    float side = 10.0f * tanf(glm_rad(30));
    box_render_shadow_tile_bounds(&tile, lo, hi);
    test_assert(fabsf(lo[1] - -5.0f) < 1e-4f);
    test_assert(fabsf(hi[1] - 5.0f) < 1e-4f);
    test_assert(lo[0] <= 1.0f - side);
    test_assert(hi[0] >= 1.0f + side);
    test_assert(lo[2] <= 2.0f - side);
    test_assert(hi[2] >= 2.0f + side);
    test_assert(lo[0] >= 1.0f - 10.0f);
    test_assert(hi[2] <= 2.0f + 10.0f);

    test_bool(box_render_shadow_tile_touches(&tile, (vec3){0.5f, 0, 1.5f}, (vec3){1.5f, 1, 2.5f}),
        true);
    test_bool(box_render_shadow_tile_touches(&tile,
        (vec3){1 + 0.9f * side, -4.5f, 2}, (vec3){1 + side, -4, 2.5f}), true);
    test_bool(box_render_shadow_tile_touches(&tile, (vec3){0, 5.5f, 1}, (vec3){2, 6, 3}), false);
    test_bool(box_render_shadow_tile_touches(&tile, (vec3){12, 0, 1}, (vec3){13, 1, 3}), false);
    test_bool(box_render_shadow_tile_touches(&tile, (vec3){0, -7, 1}, (vec3){2, -6, 3}), false);
}

/* A box in the corner of the cube around the range of a point light is outside its sphere */
void ShadowAtlas_point_tile_bounds(void) {
    // lighting/shadows
    box_render_shadow_tile_t tile = {
        .projection = BOX_RENDER_SHADOW_OCTAHEDRAL,
        .position = {1, 5, 2},
        .near_ = 0.05f,
        .far_ = 10.0f
    };
    vec3 lo, hi;
    box_render_shadow_tile_bounds(&tile, lo, hi);
    test_flt(lo[0], -9.0f);
    test_flt(hi[1], 15.0f);
    test_flt(lo[2], -8.0f);

    test_bool(box_render_shadow_tile_touches(&tile, (vec3){1, 5, 11}, (vec3){2, 6, 11.5f}), true);
    test_bool(box_render_shadow_tile_touches(&tile, (vec3){-20, 4, 1}, (vec3){20, 5, 2}), true);
    test_bool(box_render_shadow_tile_touches(&tile, (vec3){8.5f, 12.5f, 9.5f},
        (vec3){9, 13, 10}), false);
    test_bool(box_render_shadow_tile_touches(&tile, (vec3){1, 5, 12.5f}, (vec3){2, 6, 13}),
        false);
}

/* Regions of another scene are left out; a tile that renders already stays so */
void ShadowAtlas_touched_by_scene(void) {
    // lighting/shadows
    box_render_shadow_tile_t tiles[3] = {
        spot_tile((vec3){-8, 6, 0}, (vec3){0, -1, 0}, 10, 20),
        spot_tile((vec3){8, 6, 0}, (vec3){0, -1, 0}, 10, 20),
        {
            .projection = BOX_RENDER_SHADOW_OCTAHEDRAL,
            .position = {0, 6, 16},
            .near_ = 0.05f,
            .far_ = 8.0f,
            .render = true
        }
    };
    box_render_shadow_region_t regions[2] = {
        { .scene = 42, .min = {-9, 2, -1}, .max = {-7, 3, 1} },
        { .scene = 0, .min = {7, 2, -1}, .max = {9, 3, 1} }
    };
    box_render_shadow_touched(tiles, 3, 0, regions, 2);
    test_bool(tiles[0].render, false);
    test_bool(tiles[1].render, true);
    test_bool(tiles[2].render, true);

    tiles[1].render = false;
    box_render_shadow_touched(tiles, 3, 42, regions, 2);
    test_bool(tiles[0].render, true);
    test_bool(tiles[1].render, false);

    tiles[0].render = false;
    tiles[2].render = false;
    regions[1].min[1] = 20;
    regions[1].max[1] = 21;
    box_render_shadow_touched(tiles, 3, 0, regions, 2);
    test_bool(tiles[0].render, false);
    test_bool(tiles[1].render, false);
    test_bool(tiles[2].render, false);
}

/** Run a frame into the null backend and get whether the tile of a light renders in it.
 *
 * @param world The world
 * @param null The backend
 * @param light The light
 * @return Whether its tile renders
 */
static bool extract_renders(
    ecs_world_t *world,
    box_render_null_t *null,
    ecs_entity_t light)
{
    const box_render_shadow_tile_t *tile = extract_tile_of(extract_frame(world, null), light);
    test_assert(tile != NULL);

    return tile->render;
}

/* The tile of a spot light renders in the first frame and again when a caster under it, the
 * light or the atlas changes; the light extract decides it on the main thread */
void ShadowAtlas_extract_renders_changed_tiles(void) {
    // lighting/shadows
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = extract_world(&null, &view);
    ecs_entity_t light = spot_new(world, (vec3){0, 4, 0}, true, LightBakeModeMixed);
    ecs_entity_t caster = ecs_new(world);
    ecs_set(world, caster, Position, {0, 2, 0});
    ecs_set(world, caster, Box, {1, 0.1f, 1});
    ecs_entity_t far = ecs_new(world);
    ecs_set(world, far, Position, {0, 2, -40});
    ecs_set(world, far, Box, {1, 0.1f, 1});

    test_bool(extract_renders(world, null, light), true);
    test_bool(extract_renders(world, null, light), false);
    test_bool(extract_renders(world, null, light), false);

    ecs_set(world, caster, Position, {0.5f, 2, 0});
    test_bool(extract_renders(world, null, light), true);
    test_bool(extract_renders(world, null, light), false);

    ecs_set(world, far, Position, {1, 2, -40});
    test_bool(extract_renders(world, null, light), false);

    ecs_add(world, caster, NoShadow);
    test_bool(extract_renders(world, null, light), true);
    test_bool(extract_renders(world, null, light), false);

    ecs_set(world, light, Position, {0, 4.5f, 0});
    test_bool(extract_renders(world, null, light), true);
    test_bool(extract_renders(world, null, light), false);

    ecs_singleton_set(world, ShadowAtlas, {.resolution = 1024, .min_tile = 64});
    test_bool(extract_renders(world, null, light), true);
    test_bool(extract_renders(world, null, light), false);

    test_bool(box_render_shadow_cache(world, false), true);
    test_bool(extract_renders(world, null, light), true);
    test_bool(extract_renders(world, null, light), true);

    ecs_fini(world);
    box_render_null_free(null);
}
