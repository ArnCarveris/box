/**
 * @file ShadowAtlas.c
 * @brief Tests for the shadow atlas of the spot lights: its settings, the tile allocation and
 * what the light extract sends for it.
 *
 * The allocation cases call box_render_shadow_allocate directly; the extract cases render into
 * the null backend with a camera at (0, 2, 10) looking down -Z and a spot light 4 units above
 * the origin shining straight down.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "bake.h"

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
