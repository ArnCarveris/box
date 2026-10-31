/**
 * @file InstanceStore.c
 * @brief Tests for the instance pages every batch shares, as the GPU backend binds them.
 *
 * A batch per mesh is made with regular polygons of distinct side counts in front of an
 * orthographic camera, with a sun so the shadow cascades draw them as well. The polygons are
 * plain meshes, whose pages have no surface stream.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "draw.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Create a headless world with a view of the polygons, a sun and a surface.
 *
 * @return The world
 */
static ecs_world_t* world_new(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 50});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 40, .near_ = 0.1f,
        .far_ = 100});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    v->shadows.enabled = true;
    v->culling = (CullSettings){.enabled = true, .occlusion = true};
    ecs_modified(world, view, View);
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.5423041f, 2.3679806f, -0.4669112f});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 32, .height = 32, .resolution_scale = 1});

    return world;
}

/** Add the shapes of a number of batches, two per batch.
 *
 * @param world The world
 * @param batches Batches, each with its own polygon mesh
 * @param out Receives the first shape of each batch, NULL to ignore
 */
static void batches_new(
    ecs_world_t *world,
    int32_t batches,
    ecs_entity_t *out)
{
    int32_t i, j;
    for (i = 0; i < batches; i ++) {
        for (j = 0; j < 2; j ++) {
            ecs_entity_t e = ecs_new(world);
            ecs_set(world, e, Position, {(float)(i % 10) * 3 - 15 + (float)j,
                (float)(i / 10) * 3 - 8, 0});
            ecs_set(world, e, NGon, {.sides = 3 + i, .radius = 0.5f});
            ecs_set(world, e, Rgb, {200, 200, 200});
            if (out && !j) {
                out[i] = e;
            }
        }
    }
}

/** Run frames and read the counters of the GPU backend after them.
 *
 * @param world The world
 * @param count Frames to run
 * @return The counters
 */
static box_render_gpu_stats_t frames(
    ecs_world_t *world,
    int32_t count)
{
    box_render_gpu_stats_t stats;
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &stats), true);

    return stats;
}

void InstanceStore_instance_binds_do_not_grow_with_batches(void) {
    // gpu_instance_data/instance_store
    int32_t counts[2] = {5, 50}, binds[2], buffers[2], draws[2], run;
    for (run = 0; run < 2; run ++) {
        ecs_world_t *world = world_new();
        batches_new(world, counts[run], NULL);
        box_render_gpu_stats_t before = frames(world, 3);
        box_render_gpu_stats_t after = frames(world, 2);
        box_render_draws_stats_t stats;
        box_render_draws_stats(world, &stats);
        test_int(stats.batches, counts[run]);
        test_int(stats.pages, 1);
        test_int(after.errors, 0);
        binds[run] = after.instance_binds - before.instance_binds;
        draws[run] = after.draws - before.draws;
        buffers[run] = after.buffers;
        test_assert(binds[run] > 0);
        ecs_fini(world);
    }

    test_int(binds[0], binds[1]);
    test_int(buffers[0], buffers[1]);
    test_assert(draws[1] > draws[0]);
}

void InstanceStore_scatters_follow_pages_with_changes(void) {
    // gpu_instance_data/instance_store
    ecs_world_t *world = world_new();
    box_render_draws_chunk(world, 2);
    box_render_draws_page(world, 4);
    ecs_entity_t first[6];
    int32_t i;
    batches_new(world, 6, first);
    box_render_gpu_stats_t before = frames(world, 3);
    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.pages, 3);

    ecs_set(world, first[0], Position, {0, 5, 0});
    ecs_set(world, first[4], Position, {3, 5, 0});
    box_render_gpu_stats_t after = frames(world, 1);
    box_render_draws_stats(world, &stats);
    test_int(stats.scatters, 2);
    test_int(after.scatters - before.scatters, 2);
    test_int(after.scattered - before.scattered, 2);

    before = after;
    for (i = 0; i < 6; i ++) {
        ecs_set(world, first[i], Position, {(float)i * 3, 8, 0});
    }

    after = frames(world, 1);
    box_render_draws_stats(world, &stats);
    test_int(stats.scatters, 3);
    test_int(after.scatters - before.scatters, 3);
    test_int(after.scattered - before.scattered, 6);

    before = after;
    after = frames(world, 1);
    test_int(after.scatters - before.scatters, 0);
    test_int(after.errors, 0);
    ecs_fini(world);
}

/** Check that the GPU holds every stream of a batch as the extract has it.
 *
 * @param world The world
 * @param shape A shape of the batch
 */
static void batch_matches(
    ecs_world_t *world,
    ecs_entity_t shape)
{
    box_render_draws_t *d = box_draws_state(world);
    box_draw_batch_t *b = box_draws_batch(d, ecs_get(world, shape, DrawSlot)->batch);
    box_draw_store_t *store = box_draws_batch_store(d, b);
    const ecs_vec_t *streams[BOX_RENDER_INSTANCE_STREAMS] = {
        &b->transforms, &b->mesh_shadings, &b->surfaces
    };
    uint32_t count = (uint32_t)ecs_vec_count(&b->entities);
    int32_t s;
    test_assert(b->page == 0);
    for (s = 0; s < store->layout.streams; s ++) {
        uint32_t stride = store->layout.stride[s];
        uint32_t size = count * stride;
        void *gpu = ecs_os_malloc((ecs_size_t)size);
        test_bool(box_render_gpu_read_buffer(world, box_draws_store_handle(store, b->page, s),
            b->first * stride, size, gpu), true);
        test_assert(!ecs_os_memcmp(gpu, ecs_vec_first(streams[s]), (ecs_size_t)size));
        ecs_os_free(gpu);
    }
}

void InstanceStore_moved_run_and_grown_page_keep_records(void) {
    // gpu_instance_data/instance_page_copy
    ecs_world_t *world = world_new();
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_entity_t a[2], b[3], added[128];
    int32_t i;
    for (i = 0; i < 2; i ++) {
        a[i] = ecs_new(world);
        ecs_set(world, a[i], Position, {(float)i - 10, 0, 0});
        ecs_set(world, a[i], NGon, {.sides = 3, .radius = 0.5f});
        ecs_set(world, a[i], Rgb, {200, 50, 50});
    }

    for (i = 0; i < 3; i ++) {
        b[i] = ecs_new(world);
        ecs_set(world, b[i], Position, {(float)i, 5, 0});
        ecs_set(world, b[i], NGon, {.sides = 4, .radius = 0.5f});
        ecs_set(world, b[i], Rgb, {50, 200, 50});
    }

    frames(world, 3);
    batch_matches(world, a[0]);
    batch_matches(world, b[0]);

    /* The first batch outgrows its run of 16 slots and moves to a run of 84 past the second, so
     * the page grows from 32 to 116 slots in the same frame: the old buffers are copied into the
     * new ones and the two old instances from the old run into the new run */
    for (i = 0; i < 64; i ++) {
        added[i] = ecs_new(world);
        ecs_set(world, added[i], Position, {(float)(i % 16) - 8, (float)(i / 16) - 8, 0});
        ecs_set(world, added[i], NGon, {.sides = 3, .radius = 0.5f});
        ecs_set(world, added[i], Rgb, {200, 50, 50});
    }

    box_render_gpu_stats_t after = frames(world, 1);
    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.copies, BOX_RENDER_INSTANCE_STREAMS_PLAIN);
    test_int(stats.kept_pages, BOX_RENDER_INSTANCE_STREAMS_PLAIN);
    test_int(after.errors, 0);
    batch_matches(world, a[0]);
    batch_matches(world, b[0]);

    /* Moved again, to a run of 192 slots, with a changed instance among the copied ones, which
     * scatters over the copy, and a change of the second batch in the same frame */
    ecs_set(world, a[1], Position, {-12, 1, 0});
    ecs_set(world, b[1], Position, {1, 6, 0});
    for (i = 64; i < 128; i ++) {
        added[i] = ecs_new(world);
        ecs_set(world, added[i], Position, {(float)(i % 16) - 8, (float)(i / 16) - 8, 0});
        ecs_set(world, added[i], NGon, {.sides = 3, .radius = 0.5f});
        ecs_set(world, added[i], Rgb, {200, 50, 50});
    }

    after = frames(world, 1);
    box_render_draws_stats(world, &stats);
    test_int(stats.copies, BOX_RENDER_INSTANCE_STREAMS_PLAIN);
    test_int(stats.scattered, 2);
    test_int(after.errors, 0);
    batch_matches(world, a[0]);
    batch_matches(world, b[0]);

    after = frames(world, 2);
    test_int(after.errors, 0);
    batch_matches(world, a[0]);
    batch_matches(world, b[0]);
    ecs_fini(world);
}

/** Add a polygon.
 *
 * @param world The world
 * @param sides Sides of the polygon, which select its mesh
 * @param x Its x position
 * @param y Its y position
 * @return The shape
 */
static ecs_entity_t ngon_new(
    ecs_world_t *world,
    int32_t sides,
    float x,
    float y)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, y, 0});
    ecs_set(world, e, NGon, {.sides = sides, .radius = 0.5f});
    ecs_set(world, e, Rgb, {(uint8_t)(40 * sides), 50, 50});

    return e;
}

void InstanceStore_moved_run_reused_in_same_frame(void) {
    // gpu_instance_data/instance_page_copy
    ecs_world_t *world = world_new();
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_entity_t a[4], b[4], c = 0;
    int32_t i;
    for (i = 0; i < 4; i ++) {
        a[i] = ngon_new(world, 3, (float)i - 10, 0);
        b[i] = ngon_new(world, 4, (float)i - 10, 4);
    }

    frames(world, 3);
    box_render_draws_t *d = box_draws_state(world);
    test_uint(box_draws_batch(d, ecs_get(world, a[0], DrawSlot)->batch)->first, 0);
    test_uint(box_draws_batch(d, ecs_get(world, b[0], DrawSlot)->batch)->first,
        BOX_DRAWS_MIN_CAPACITY);

    /* The first batch moves past the second, and a new batch with the mesh of the second takes
     * the run it left in the same frame: its upload writes the old run after the copy read it.
     * The second batch scatters a change in the same frame */
    for (i = 0; i < 64; i ++) {
        ngon_new(world, 3, (float)(i % 16) - 8, (float)(i / 16) - 8);
    }

    for (i = 0; i < 3; i ++) {
        c = ngon_new(world, 4, (float)i, 8);
        ecs_add(world, c, DynamicTransform);
    }

    ecs_set(world, b[1], Position, {-9, 5, 0});
    box_render_gpu_stats_t after = frames(world, 1);
    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_uint(box_draws_batch(d, ecs_get(world, a[0], DrawSlot)->batch)->first,
        2 * BOX_DRAWS_MIN_CAPACITY);
    test_uint(box_draws_batch(d, ecs_get(world, c, DrawSlot)->batch)->first, 0);
    test_int(stats.copies, BOX_RENDER_INSTANCE_STREAMS_PLAIN);
    test_int(stats.scattered, 1);
    test_int(after.errors, 0);
    batch_matches(world, a[0]);
    batch_matches(world, b[0]);
    batch_matches(world, c);

    after = frames(world, 2);
    test_int(after.errors, 0);
    batch_matches(world, a[0]);
    batch_matches(world, b[0]);
    batch_matches(world, c);
    ecs_fini(world);
}

/** Check that a pixel of a capture has a channel above a level and the others below another.
 *
 * @param px The pixels of the 64x64 capture
 * @param x The column
 * @param y The row, from the top
 * @param channel The channel that is lit, 0 red, 1 green, 2 blue, 3 for all three
 */
static void test_lit(
    const unsigned char *px,
    int32_t x,
    int32_t y,
    int32_t channel)
{
    const unsigned char *p = &px[(y * 64 + x) * 4];
    int32_t c;
    for (c = 0; c < 3; c ++) {
        if (channel == 3 || c == channel) {
            test_assert(p[c] > 150);
        } else {
            test_assert(p[c] < 60);
        }
    }
}

/** A 2D shape, the glyphs of a text and plain meshes draw in one frame from pages without a
 * surface buffer, which bind the zero record of the backend; the page of a shape with a
 * parametric surface has one. */
void InstanceStore_plain_pages_bind_no_surface_buffer(void) {
    // gpu_instance_data/instance_surface_mesh
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 50});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});

    /* Top left a red rect, top right a white text */
    ecs_entity_t rect = ecs_new(world);
    ecs_set(world, rect, Position, {-16, 16, 0});
    ecs_set(world, rect, Rect, {20, 12});
    ecs_set(world, rect, Rgb, {255, 0, 0});
    ecs_entity_t font = ecs_entity(world, { .name = "surface_mesh_font" });
    ecs_set(world, font, Font, { .path = "test/assets/fonts/B612-Regular.ttf" });
    ecs_entity_t text = ecs_new(world);
    ecs_set(world, text, Position, {16, 16, 0});
    ecs_set(world, text, Text, {.value = "HH", .size = 14, .font = font});
    ecs_set(world, text, Rgb, {255, 255, 255});

    /* Bottom left two green boxes of one plain batch: slot 1 is past the zero record */
    ecs_entity_t boxes[2];
    int32_t i;
    for (i = 0; i < 2; i ++) {
        boxes[i] = ecs_new(world);
        ecs_set(world, boxes[i], Position, {-24 + (float)i * 14, -16, 0});
        ecs_set(world, boxes[i], Box, {8, 8, 8});
        ecs_set(world, boxes[i], Rgb, {0, 0, 0});
        ecs_set(world, boxes[i], Emissive, {.color = {0, 255, 0}, .strength = 1});
    }

    /* Bottom right a blue torus facing the camera */
    ecs_entity_t torus = ecs_new(world);
    ecs_set(world, torus, Position, {16, -16, 0});
    ecs_set(world, torus, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, torus, TorusShell, {
        .radius = 9, .tube = 3,
        .ring_start = 0, .ring_end = 2 * (float)GLM_PI,
        .tube_start = 0, .tube_end = 2 * (float)GLM_PI });
    ecs_set(world, torus, Rgb, {0, 0, 0});
    ecs_set(world, torus, Emissive, {.color = {0, 0, 255}, .strength = 1});

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .resolution_scale = 1,
        .write_to_file = "surface_mesh.png"});
    box_render_gpu_stats_t stats = frames(world, 4);
    test_int(stats.errors, 0);

    /* The 2D page and the plain mesh page have their transform and shading buffers, and no
     * surface buffer; the page of the torus has all three */
    box_render_draws_t *d = box_draws_state(world);
    box_draw_store_t *plain = &d->stores[BOX_DRAWS_STORE_MESH];
    box_draw_batch_t *b = box_draws_batch(d, ecs_get(world, rect, DrawSlot)->batch);
    test_int(b->page, 0);
    const TextSlots *slots = ecs_get(world, text, TextSlots);
    test_assert(slots != NULL && ecs_vec_count(&slots->slots) == 2);
    b = box_draws_batch(d, ecs_vec_first_t(&slots->slots, box_draw_glyph_slot_t)->batch);
    test_int(b->page, 0);
    test_assert(box_draws_batch_store(d, b) == &d->stores[BOX_DRAWS_STORE_2D]);
    test_assert(box_render_gpu_buffer_size(world,
        BOX_RENDER_BUFFER_FIRST_2D + BOX_RENDER_STREAM_TRANSFORM) > 0);
    test_assert(box_render_gpu_buffer_size(world,
        BOX_RENDER_BUFFER_FIRST_2D + BOX_RENDER_STREAM_SHADING) > 0);
    test_uint(box_render_gpu_buffer_size(world,
        BOX_RENDER_BUFFER_FIRST_2D + BOX_RENDER_STREAM_SURFACE), 0);

    b = box_draws_batch(d, ecs_get(world, boxes[0], DrawSlot)->batch);
    test_int(ecs_vec_count(&b->entities), 2);
    test_int(b->page, 0);
    test_assert(box_draws_batch_store(d, b) == plain);
    test_uint(box_draws_store_handle(plain, 0, BOX_RENDER_STREAM_TRANSFORM),
        BOX_RENDER_BUFFER_FIRST);
    test_assert(box_render_gpu_buffer_size(world,
        box_draws_store_handle(plain, 0, BOX_RENDER_STREAM_TRANSFORM)) > 0);
    test_assert(box_render_gpu_buffer_size(world,
        box_draws_store_handle(plain, 0, BOX_RENDER_STREAM_SHADING)) > 0);
    test_uint(box_render_gpu_buffer_size(world,
        box_draws_store_handle(plain, 0, BOX_RENDER_STREAM_SURFACE)), 0);

    b = box_draws_batch(d, ecs_get(world, torus, DrawSlot)->batch);
    test_assert(b->key.surface);
    box_draw_store_t *store = box_draws_batch_store(d, b);
    test_assert(store == &d->stores[BOX_DRAWS_STORE_SURFACE]);
    test_assert(box_render_gpu_buffer_size(world,
        box_draws_store_handle(store, b->page, BOX_RENDER_STREAM_SURFACE)) >=
            sizeof(box_render_surface_t));

    /* Every shape rendered */
    char *path = flecs_asprintf("%s/surface_mesh.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 64);
    test_int(h, 64);
    test_lit(px, 16, 16, 0);
    test_lit(px, 8, 48, 1);
    test_lit(px, 22, 48, 1);
    test_lit(px, 39, 48, 2);
    test_lit(px, 57, 48, 2);
    test_assert(px[(48 * 64 + 48) * 4 + 2] < 60);

    int32_t x, y, lit = 0;
    for (y = 0; y < 32; y ++) {
        for (x = 33; x < 64; x ++) {
            const unsigned char *p = &px[(y * 64 + x) * 4];
            lit += p[0] > 150 && p[1] > 150 && p[2] > 150;
        }
    }

    test_assert(lit > 10);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}
