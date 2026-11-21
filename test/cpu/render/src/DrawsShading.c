/**
 * @file DrawsShading.c
 * @brief Tests for the shading-only update of a subtree: an Opacity or a Tint on an ancestor
 * rebuilds the shading of the shapes below without recomputing their batch key, unless the
 * alpha moves them between the opaque and the blended batches.
 *
 * Every case renders with the null backend and reads the batches and slots of the extract.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "draw.h"
#include <string.h>

/** Levels of the hierarchy below the root */
#define DEPTH (6)

/** Shapes on each level */
#define WIDTH (3)

/** Shapes of the hierarchy below the root */
#define COUNT (DEPTH * WIDTH)

/** Create a world that renders with a null backend, has a view and a scene with blend modes.
 *
 * @return The world
 */
static ecs_world_t* world_new(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_singleton_set(world, Scene, {.shader_features.Extended.blend_modes = true});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});

    return world;
}

/** Add a box.
 *
 * @param world The world
 * @param parent Parent of the box, 0 for none
 * @param x Its x position
 * @return The box
 */
static ecs_entity_t box_new(
    ecs_world_t *world,
    ecs_entity_t parent,
    float x)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, e, Position, {x, 0, 0});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_set(world, e, Rgb, {200, 100, 50});

    return e;
}

/** Add a hierarchy of DEPTH levels below a root, WIDTH boxes on each level, each level a child
 * of the first box of the level above.
 *
 * @param world The world
 * @param root The root
 * @param out Receives the COUNT boxes
 */
static void tree_new(
    ecs_world_t *world,
    ecs_entity_t root,
    ecs_entity_t *out)
{
    ecs_entity_t parent = root;
    int32_t l, w;
    for (l = 0; l < DEPTH; l ++) {
        for (w = 0; w < WIDTH; w ++) {
            out[l * WIDTH + w] = box_new(world, parent, (float)w);
        }

        parent = out[l * WIDTH];
    }
}

/** Run a frame and wait until the backend has its packet.
 *
 * @param world The world
 */
static void frame(
    ecs_world_t *world)
{
    ecs_progress(world, 0);
    box_render_sync(world);
}

/** Get the batch of a shape.
 *
 * @param world The world
 * @param e The shape
 * @return Its batch
 */
static box_draw_batch_t* batch_of(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const DrawSlot *slot = ecs_get(world, e, DrawSlot);
    test_assert(slot != NULL && slot->batch >= 0);

    return box_draws_batch(box_draws_state(world), slot->batch);
}

/** Get the mesh shading of a shape.
 *
 * @param world The world
 * @param e The shape
 * @return The shading of its slot
 */
static const box_render_mesh_shading_t* shading_of(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const DrawSlot *slot = ecs_get(world, e, DrawSlot);
    test_assert(slot != NULL && slot->batch >= 0);

    return box_draws_slot_mesh_shading(batch_of(world, e), slot->slot);
}

/** Check that the fade and the tints of a shape are those the up search of its ancestors
 * computes, bit for bit.
 *
 * @param world The world
 * @param e The shape
 */
static void test_fade(
    ecs_world_t *world,
    ecs_entity_t e)
{
    box_draw_tint_t tint;
    const DrawSlot *slot = ecs_get(world, e, DrawSlot);
    float fade = box_draws_fade(world, e, &tint);
    test_assert(slot != NULL);
    test_assert(memcmp(&slot->fade, &fade, sizeof(float)) == 0);
    test_assert(memcmp(&slot->tint, &tint, sizeof(box_draw_tint_t)) == 0);
}

/** Check the blend and the alpha of the shapes of a hierarchy.
 *
 * @param world The world
 * @param shapes The shapes
 * @param blended Whether they draw in a blended batch
 * @param alpha The alpha of their shading
 */
static void test_shapes(
    ecs_world_t *world,
    const ecs_entity_t *shapes,
    bool blended,
    float alpha)
{
    int32_t i;
    for (i = 0; i < COUNT; i ++) {
        test_bool(batch_of(world, shapes[i])->key.blend != Opaque, blended);
        test_flt(shading_of(world, shapes[i])->alpha, alpha);
        test_fade(world, shapes[i]);
    }
}

void DrawsShading_opacity_fade_moves_to_blended_and_back(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t root = box_new(world, 0, 0), shapes[COUNT];
    tree_new(world, root, shapes);
    frame(world);
    test_shapes(world, shapes, false, 1.0f);
    int32_t opaque = ecs_get(world, shapes[COUNT - 1], DrawSlot)->batch;

    ecs_set(world, root, Opacity, {0.5f});
    frame(world);
    test_shapes(world, shapes, true, 0.5f);
    test_bool(batch_of(world, root)->key.blend != Opaque, true);
    test_assert(ecs_get(world, shapes[COUNT - 1], DrawSlot)->batch != opaque);

    ecs_set(world, root, Opacity, {1.0f});
    frame(world);
    test_shapes(world, shapes, false, 1.0f);
    test_int(batch_of(world, root)->key.blend, Opaque);
    test_int(ecs_get(world, shapes[COUNT - 1], DrawSlot)->batch, opaque);

    ecs_set(world, root, Opacity, {0.25f});
    frame(world);
    test_shapes(world, shapes, true, 0.25f);

    ecs_remove(world, root, Opacity);
    frame(world);
    test_shapes(world, shapes, false, 1.0f);
    test_int(ecs_get(world, shapes[COUNT - 1], DrawSlot)->batch, opaque);

    ecs_fini(world);
}

void DrawsShading_opacity_fade_keeps_blended_batch(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t root = box_new(world, 0, 0), shapes[COUNT];
    tree_new(world, root, shapes);
    ecs_set(world, root, Opacity, {0.5f});
    frame(world);
    test_shapes(world, shapes, true, 0.5f);
    int32_t batch = ecs_get(world, shapes[COUNT - 1], DrawSlot)->batch;
    int32_t slot = ecs_get(world, shapes[COUNT - 1], DrawSlot)->slot;

    ecs_set(world, root, Opacity, {0.75f});
    frame(world);
    test_shapes(world, shapes, true, 0.75f);
    test_int(ecs_get(world, shapes[COUNT - 1], DrawSlot)->batch, batch);
    test_int(ecs_get(world, shapes[COUNT - 1], DrawSlot)->slot, slot);

    ecs_set(world, shapes[0], BlendMode, {Additive});
    frame(world);
    test_int(batch_of(world, shapes[0])->key.blend, Additive);
    ecs_set(world, root, Opacity, {1.0f});
    frame(world);
    test_int(batch_of(world, shapes[0])->key.blend, Additive);
    test_flt(shading_of(world, shapes[0])->alpha, 1.0f);
    test_int(batch_of(world, shapes[1])->key.blend, Opaque);

    ecs_fini(world);
}

void DrawsShading_nested_opacity_matches_up_search(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t root = box_new(world, 0, 0), shapes[COUNT];
    tree_new(world, root, shapes);
    frame(world);

    ecs_set(world, root, Opacity, {0.9f});
    ecs_set(world, shapes[WIDTH], Opacity, {0.7f});
    ecs_set(world, shapes[3 * WIDTH], Opacity, {0.3f});
    ecs_set(world, shapes[2 * WIDTH], Tint, {10, 200, 30, 100});
    frame(world);

    int32_t i;
    for (i = 0; i < COUNT; i ++) {
        test_fade(world, shapes[i]);
        test_bool(batch_of(world, shapes[i])->key.blend != Opaque, true);
    }

    ecs_set(world, shapes[WIDTH], Opacity, {0.6f});
    frame(world);
    for (i = 0; i < COUNT; i ++) {
        test_fade(world, shapes[i]);
    }

    test_flt(shading_of(world, shapes[COUNT - 1])->alpha, 0.9f * 0.6f * 0.3f);

    ecs_fini(world);
}

void DrawsShading_tint_updates_descendant_shading(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t root = box_new(world, 0, 0), shapes[COUNT];
    tree_new(world, root, shapes);
    frame(world);
    box_render_mesh_shading_t plain = *shading_of(world, shapes[COUNT - 1]);
    int32_t batch = ecs_get(world, shapes[COUNT - 1], DrawSlot)->batch;
    test_bool(ecs_get(world, shapes[COUNT - 1], DrawSlot)->tinted, false);

    ecs_set(world, root, Tint, {255, 0, 0, 128});
    frame(world);
    int32_t i;
    for (i = 0; i < COUNT; i ++) {
        test_bool(ecs_get(world, shapes[i], DrawSlot)->tinted, true);
        test_fade(world, shapes[i]);
        test_int(batch_of(world, shapes[i])->key.blend, Opaque);
    }

    const box_render_mesh_shading_t *tinted = shading_of(world, shapes[COUNT - 1]);
    test_assert(memcmp(tinted->tint, plain.tint, sizeof(plain.tint)) != 0);
    test_uint(tinted->material, plain.material);
    test_flt(tinted->alpha, 1.0f);
    test_int(ecs_get(world, shapes[COUNT - 1], DrawSlot)->batch, batch);
    box_render_mesh_shading_t red = *tinted;

    ecs_set(world, root, Tint, {0, 0, 255, 128});
    frame(world);
    test_assert(memcmp(shading_of(world, shapes[COUNT - 1])->tint, red.tint,
        sizeof(red.tint)) != 0);
    test_fade(world, shapes[COUNT - 1]);

    ecs_remove(world, root, Tint);
    frame(world);
    for (i = 0; i < COUNT; i ++) {
        test_bool(ecs_get(world, shapes[i], DrawSlot)->tinted, false);
    }

    test_assert(memcmp(shading_of(world, shapes[COUNT - 1])->tint, plain.tint,
        sizeof(plain.tint)) == 0);

    ecs_fini(world);
}

/** Ancestor tags of test_tagged_ancestor */
typedef enum tagged_t {
    TaggedDynamicMaterial,
    TaggedNoShadow,
    TaggedLightFixture
} tagged_t;

/** Fade the root of a hierarchy below an ancestor with a tag, within the blended class and
 * across it, and check that the shapes keep the key flags the tag gives them.
 *
 * @param tagged The tag of the ancestor
 * @param dynamic_material Whether the shapes have key.dynamic_material
 * @param shadow Whether the shapes have key.shadow
 * @param fixture Whether the shapes have key.fixture
 */
static void test_tagged_ancestor(
    tagged_t tagged,
    bool dynamic_material,
    bool shadow,
    bool fixture)
{
    ecs_world_t *world = world_new();
    ecs_entity_t tags[] = { DynamicMaterial, NoShadow, LightFixture };
    ecs_entity_t ancestor = ecs_new(world);
    ecs_add_id(world, ancestor, tags[tagged]);
    ecs_entity_t root = box_new(world, ancestor, 0), shapes[COUNT];
    tree_new(world, root, shapes);
    frame(world);

    float fades[] = {0.5f, 0.4f, 1.0f, 1.0f, 0.8f};
    int32_t f, i;
    for (f = 0; f < (int32_t)(sizeof(fades) / sizeof(fades[0])); f ++) {
        ecs_set(world, root, Opacity, {fades[f]});
        frame(world);
        for (i = 0; i < COUNT; i ++) {
            const box_draw_key_t *key = &batch_of(world, shapes[i])->key;
            test_bool(key->dynamic_material, dynamic_material);
            test_bool(key->shadow, shadow);
            test_bool(key->fixture, fixture);
            test_bool(key->blend != Opaque, fades[f] < 1.0f);
            test_flt(shading_of(world, shapes[i])->alpha, fades[f]);
        }
    }

    ecs_fini(world);
}

void DrawsShading_dynamic_material_ancestor_keeps_key(void) {
    test_tagged_ancestor(TaggedDynamicMaterial, true, true, false);
}

void DrawsShading_no_shadow_ancestor_keeps_key(void) {
    test_tagged_ancestor(TaggedNoShadow, false, false, false);
}

void DrawsShading_light_fixture_ancestor_keeps_key(void) {
    test_tagged_ancestor(TaggedLightFixture, false, true, true);
}

void DrawsShading_fade_with_other_change_rekeys(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t root = box_new(world, 0, 0), shapes[COUNT];
    tree_new(world, root, shapes);
    ecs_set(world, root, Opacity, {0.5f});
    frame(world);
    test_int(batch_of(world, shapes[1])->key.blend, AlphaBlend);

    ecs_set(world, root, Opacity, {0.6f});
    ecs_set(world, shapes[1], BlendMode, {Additive});
    ecs_set(world, shapes[2], BackFaceMode, {BackFacePlain});
    frame(world);
    test_int(batch_of(world, shapes[1])->key.blend, Additive);
    test_int(batch_of(world, shapes[2])->key.back_face, BackFacePlain);
    test_int(batch_of(world, shapes[2])->key.blend, AlphaBlend);
    test_shapes(world, shapes, true, 0.6f);

    ecs_fini(world);
}
