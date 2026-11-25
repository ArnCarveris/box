/**
 * @file TextureLayers.c
 * @brief Tests for the TextureLayer and TextureLayers material components: their defaults and
 * reflection, the values a layer packs, the batch keys of shapes with and without layers, the
 * records of their draws and that writing the amount of a layer does not move a shape.
 *
 * The extract cases render with the null backend and read the batch keys and the packets it
 * received.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"
#include "draw.h"

/** RGB PNG of 4 by 2 pixels, the albedo of a layer */
#define LAYERS_RGB_PNG "test/assets/textures/rgb.png"

/** Grayscale PNG of 4 by 2 pixels, the mask of a layer */
#define LAYERS_GRAY_PNG "test/assets/textures/gray.png"

/** Create a world that renders into the null backend with a view.
 *
 * @param null Receives the backend
 * @return The world
 */
static ecs_world_t* layers_world(
    box_render_null_t **null)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 2, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});

    return world;
}

/** Run a frame and wait until the backend has its packet.
 *
 * @param world The world
 */
static void layers_frame(
    ecs_world_t *world)
{
    ecs_progress(world, 1.0f / 64.0f);
    box_render_sync(world);
}

/** Add a unit box.
 *
 * @param world The world
 * @param x Its x position
 * @return The box
 */
static ecs_entity_t layers_box(
    ecs_world_t *world,
    float x)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, 0, 0});
    ecs_set(world, e, Box, {1, 1, 1});

    return e;
}

/** Get the index of the batch of a shape as the extract keeps it.
 *
 * @param world The world
 * @param e The shape
 * @return Its batch index
 */
static int32_t layers_batch_index(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const DrawSlot *slot = ecs_get(world, e, DrawSlot);
    test_assert(slot != NULL && slot->batch >= 0);

    return slot->batch;
}

/** Get the batch of a shape as the extract keeps it.
 *
 * @param world The world
 * @param e The shape
 * @return Its batch
 */
static box_draw_batch_t* layers_batch(
    ecs_world_t *world,
    ecs_entity_t e)
{
    return box_draws_batch(box_draws_state(world), layers_batch_index(world, e));
}

/** Find the opaque draw of the batch of a shape in the last packet.
 *
 * @param world The world
 * @param null The backend
 * @param e The shape
 * @return The draw
 */
static const box_render_batch_t* layers_draw(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e)
{
    const box_draw_batch_t *b = layers_batch(world, e);
    const box_render_batch_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.batches); i ++) {
        const box_render_batch_t *draw = ecs_vec_get_t(
            &null->last.batches, box_render_batch_t, i);
        if (draw->mesh == b->key.mesh && draw->view == -1 && draw->first_instance == b->first &&
            draw->instances == box_draws_store_handle(box_draws_batch_store(
                box_draws_state(world), b), b->page, BOX_RENDER_STREAM_TRANSFORM))
        {
            result = draw;
        }
    }

    test_assert(result != NULL);

    return result;
}

/** Get the TextureLayers record of the draw of a shape in the last packet.
 *
 * @param world The world
 * @param null The backend
 * @param e The shape, which draws with layers
 * @return The record
 */
static const box_render_texture_layers_t* layers_record(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e)
{
    const box_render_batch_t *draw = layers_draw(world, null, e);
    test_bool(draw->texture_layers, true);

    return box_render_packet_data(&null->last, draw->layers);
}

/** Create a texture entity that loads an image of the test assets.
 *
 * @param world The world
 * @param path The image, from the repository root
 * @return The texture entity
 */
static ecs_entity_t layers_texture(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, TexturePath, {.path = ECS_CONST_CAST(char*, path)});

    return result;
}

/** Run frames until no texture of a list is loading, then two more so the backend has them.
 *
 * @param world The world
 * @param textures The texture entities
 * @param count Number of texture entities
 */
static void layers_wait(
    ecs_world_t *world,
    const ecs_entity_t *textures,
    int32_t count)
{
    int32_t frames, i;
    for (frames = 0; frames < 200; frames ++) {
        bool loading = false;
        layers_frame(world);
        for (i = 0; i < count; i ++) {
            const TextureStatus *status = ecs_get(world, textures[i], TextureStatus);
            loading |= !status || status->state == TextureLoading;
        }

        if (!loading) {
            break;
        }
    }

    layers_frame(world);
    layers_frame(world);
}

void TextureLayers_defaults(void) {
    // materials/texture_layers
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, TextureLayer);
    const TextureLayer *l = ecs_get(world, added, TextureLayer);
    test_assert(l != NULL);
    test_flt(l->amount, BOX_MATERIAL_LAYER_AMOUNT);
    test_flt(l->amount, 1);
    test_flt(l->direction[0], 0);
    test_flt(l->direction[1], 1);
    test_flt(l->direction[2], 0);
    test_flt(l->facing, 0);
    test_uint(l->mask, 0);
    test_flt(l->cavity, 0);
    test_flt(l->contrast, BOX_MATERIAL_LAYER_CONTRAST);
    test_int(l->albedo, LayerMix);
    test_int(l->roughness, LayerMix);
    test_int(l->metallic, LayerKeep);
    test_int(l->occlusion, LayerKeep);
    test_flt(l->normal, 0);
    test_flt(l->porosity, 0);

    /* A layer is no material component: it brings no slot of the material table */
    test_bool(ecs_has(world, added, MaterialId), false);

    ecs_add(world, added, TextureLayers);
    const TextureLayers *set = ecs_get(world, added, TextureLayers);
    test_assert(set != NULL);
    test_uint(set->layers[0], 0);
    test_uint(set->layers[3], 0);

    /* Every member is reflected */
    test_assert(ecs_script_run(world, "layers",
        "using box.material\n"
        "wet { texture.Layer: {amount: 0.5, direction: {0, 0, 2}, facing: 0.6, cavity: -0.5,\n"
        "    contrast: 4, albedo: LayerMultiply, roughness: LayerMultiply,\n"
        "    metallic: LayerMix, occlusion: LayerMix, normal: 0.3, porosity: 1} }\n"
        "plain { texture.Layer: {} }\n"
        "rock { texture.Layers: {layers: [wet, plain]} }\n", NULL) == 0);
    ecs_entity_t wet = ecs_lookup(world, "wet");
    ecs_entity_t plain = ecs_lookup(world, "plain");
    l = ecs_get(world, wet, TextureLayer);
    test_assert(l != NULL);
    test_flt(l->amount, 0.5f);
    test_flt(l->direction[2], 2);
    test_flt(l->facing, 0.6f);
    test_flt(l->cavity, -0.5f);
    test_flt(l->contrast, 4);
    test_int(l->albedo, LayerMultiply);
    test_int(l->roughness, LayerMultiply);
    test_int(l->metallic, LayerMix);
    test_int(l->occlusion, LayerMix);
    test_flt(l->normal, 0.3f);
    test_flt(l->porosity, 1);
    l = ecs_get(world, plain, TextureLayer);
    test_flt(l->amount, 1);
    test_flt(l->contrast, 8);
    test_int(l->albedo, LayerMix);
    set = ecs_get(world, ecs_lookup(world, "rock"), TextureLayers);
    test_assert(set != NULL);
    test_uint(set->layers[0], wet);
    test_uint(set->layers[1], plain);
    test_uint(set->layers[2], 0);

    test_assert(ecs_lookup(world, "box.material.texture.Layer") == ecs_id(TextureLayer));
    test_assert(ecs_lookup(world, "box.material.texture.Layers") == ecs_id(TextureLayers));

    /* An instance inherits the layers of its prefab */
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, TextureLayers, {.layers = {wet}});
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    test_bool(ecs_owns(world, inst, TextureLayers), false);
    test_uint(ecs_get(world, inst, TextureLayers)->layers[0], wet);

    ecs_fini(world);
}

void TextureLayers_scene_feature(void) {
    // materials/texture_layers
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_uint(box_render_scene_features(world, 0) & BOX_RENDER_FEATURE_TEXTURE_LAYERS, 0);
    ecs_singleton_set(world, Scene, {.shader_features.Extended.texture_layers = true});
    test_uint(box_render_scene_features(world, 0), BOX_RENDER_FEATURE_TEXTURE_LAYERS);

    /* The feature compiles no override: the pipelines of the shapes without layers are those
     * of a scene without it */
    uint32_t shaders[2] = { BOX_RENDER_SHADER_MESH, BOX_RENDER_SHADER_MESH_EXTENDED };
    int32_t s;
    for (s = 0; s < 2; s ++) {
        test_uint(box_render_mesh_overrides(shaders[s], BOX_RENDER_FEATURE_TEXTURE_LAYERS,
            false, false), 0);
        test_uint(box_render_mesh_overrides(shaders[s], BOX_RENDER_FEATURE_TEXTURE_LAYERS |
            BOX_RENDER_FEATURE_SHEEN, false, false), box_render_mesh_overrides(shaders[s],
            BOX_RENDER_FEATURE_SHEEN, false, false));
    }

    ecs_fini(world);
}

void TextureLayers_pack(void) {
    // materials/texture_layers
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_texture_layer_t v;

    /* A layer of defaults: white, matte, every amount, up */
    ecs_entity_t snow = ecs_new(world);
    ecs_add(world, snow, TextureLayer);
    box_draws_layer_pack(world, snow, 0.0, &v);
    test_flt(v.color[0], 1);
    test_flt(v.color[1], 1);
    test_flt(v.color[2], 1);
    test_flt(v.color[3], 1);
    test_flt(v.direction[0], 0);
    test_flt(v.direction[1], 1);
    test_flt(v.direction[2], 0);
    test_flt(v.direction[3], 0);
    test_flt(v.coverage[0], 0);
    test_flt(v.coverage[1], 1.0f / BOX_MATERIAL_LAYER_CONTRAST);
    test_flt(v.coverage[2], 0);
    test_flt(v.coverage[3], 0);
    test_flt(v.material[0], BOX_MATERIAL_ROUGHNESS);
    test_flt(v.material[1], BOX_MATERIAL_METALLIC);
    test_flt(v.material[2], 1);
    test_flt(v.material[3], 0);
    test_flt(v.transform[0], 1);
    test_flt(v.transform[1], 1);
    test_flt(v.transform[2], 0);
    test_flt(v.transform[3], 0);
    test_uint(v.modes[0], LayerMix);
    test_uint(v.modes[1], LayerMix);
    test_uint(v.modes[2], LayerKeep);
    test_uint(v.modes[3], LayerKeep);
    test_uint(v.flags[0], 0);

    /* The wet layer of the docs: grey constants in linear light, clamped fields, a normalized
     * direction, the edge width of its contrast */
    ecs_entity_t mask = ecs_new(world);
    ecs_entity_t wet = ecs_new(world);
    ecs_set(world, wet, TextureLayer, {.amount = 3, .direction = {0, 0, 4}, .facing = -1,
        .mask = mask, .cavity = 2, .contrast = 2, .albedo = LayerMultiply,
        .roughness = LayerMultiply, .metallic = LayerKeep, .occlusion = LayerKeep, .normal = 0.3f,
        .porosity = 5});
    ecs_set(world, wet, Rgb, {153, 153, 153});
    ecs_set(world, wet, PbrMaterial, {.metallic = 0, .roughness = 0.3f});
    ecs_set(world, wet, TextureMapping, {.mode = MappingUv, .size = 2, .blend = 0.4f});
    ecs_set(world, wet, TextureTransform, {.scale_x = 2, .scale_y = 3, .offset_x = 0.25f});
    ecs_set(world, wet, PbrTextures, {.albedo = mask, .normal = mask});
    box_draws_layer_pack(world, wet, 0.0, &v);
    test_assert(v.color[0] > 0.3f && v.color[0] < 0.33f);
    test_flt(v.color[0], v.color[2]);
    test_flt(v.color[3], 1);
    test_flt(v.direction[2], 1);
    test_flt(v.direction[3], 0);
    test_flt(v.coverage[0], 1);
    test_flt(v.coverage[1], 0.5f);
    test_flt(v.coverage[2], 0.3f);
    test_flt(v.coverage[3], 1);
    test_flt(v.material[0], 0.3f);
    test_flt(v.material[1], 0);
    test_flt(v.material[2], 2);
    test_flt(v.material[3], 0.4f);
    test_flt(v.transform[0], 2);
    test_flt(v.transform[1], 3);
    test_flt(v.transform[2], 0.25f);
    test_uint(v.modes[0], LayerMultiply);
    test_uint(v.modes[1], LayerMultiply);
    test_uint(v.flags[0], BOX_RENDER_LAYER_HAS_ALBEDO | BOX_RENDER_LAYER_HAS_NORMAL |
        BOX_RENDER_LAYER_HAS_MASK | BOX_RENDER_LAYER_UV);

    /* A zero or broken direction is up, a contrast that is not positive the default, a world
     * mapping no UV flag */
    ecs_set(world, wet, TextureLayer, {.amount = -1, .direction = {0, 0, 0}, .contrast = 0,
        .albedo = 7});
    ecs_set(world, wet, TextureMapping, {.mode = MappingWorld, .size = -1});
    box_draws_layer_pack(world, wet, 0.0, &v);
    test_flt(v.color[3], 0);
    test_flt(v.direction[1], 1);
    test_flt(v.coverage[1], 1.0f / BOX_MATERIAL_LAYER_CONTRAST);
    test_flt(v.material[2], 1);
    test_uint(v.modes[0], LayerKeep);
    test_uint(v.flags[0] & BOX_RENDER_LAYER_UV, 0);

    /* The TextureScroll of the layer moves its offset by the time */
    ecs_set(world, wet, TextureScroll, {.velocity = {0.5f, 0}});
    box_draws_layer_pack(world, wet, 1.0, &v);
    test_flt(v.transform[2], 0.75f);

    /* An entity without TextureLayer packs zero, which does nothing */
    box_draws_layer_pack(world, mask, 0.0, &v);
    test_flt(v.color[3], 0);
    test_flt(v.color[0], 0);

    ecs_fini(world);
}

/** A layer packs its altitude relative to the render origin and its altitude_fade, positive to
 * cover above the altitude, negative to cover below it; a fade of 0 or one that is not finite
 * packs zero, which ignores the altitude */
void TextureLayers_altitude_pack(void) {
    // materials/texture_layers
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_texture_layer_t v;
    float altitude, fade;
    ecs_entity_t layer = ecs_new(world);

    ecs_set(world, layer, TextureLayer, {.amount = 1, .altitude = 5, .altitude_fade = 2});
    box_draws_layer_pack(world, layer, 0.0, &v);
    memcpy(&altitude, &v.flags[1], sizeof(float));
    memcpy(&fade, &v.flags[2], sizeof(float));
    test_flt(altitude, 5);
    test_flt(fade, 2);

    ecs_set(world, layer, TextureLayer, {.amount = 1, .altitude = 5, .altitude_fade = -2});
    box_draws_layer_pack(world, layer, 0.0, &v);
    memcpy(&altitude, &v.flags[1], sizeof(float));
    memcpy(&fade, &v.flags[2], sizeof(float));
    test_flt(altitude, 5);
    test_flt(fade, -2);

    ecs_entity_t focus = ecs_new(world);
    ecs_set(world, focus, Position, {0, 3000, 0});
    ecs_add(world, focus, WorldFocus);
    ecs_progress(world, 0);
    box_draws_layer_pack(world, layer, 0.0, &v);
    memcpy(&altitude, &v.flags[1], sizeof(float));
    memcpy(&fade, &v.flags[2], sizeof(float));
    test_flt(altitude, 5 - 3072);
    test_flt(fade, -2);

    ecs_set(world, layer, TextureLayer, {.amount = 1, .altitude = 5, .altitude_fade = 0});
    box_draws_layer_pack(world, layer, 0.0, &v);
    test_uint(v.flags[1], 0);
    test_uint(v.flags[2], 0);

    ecs_set(world, layer, TextureLayer, {.amount = 1, .altitude = 5, .altitude_fade = -INFINITY});
    box_draws_layer_pack(world, layer, 0.0, &v);
    test_uint(v.flags[1], 0);
    test_uint(v.flags[2], 0);

    ecs_set(world, layer, TextureLayer, {.amount = 1, .altitude = NAN, .altitude_fade = -2});
    box_draws_layer_pack(world, layer, 0.0, &v);
    test_uint(v.flags[1], 0);
    test_uint(v.flags[2], 0);

    ecs_fini(world);
}

/** Without Scene.texture_layers a shape with TextureLayers keys and draws as without them */
void TextureLayers_batches_without_feature(void) {
    // materials/texture_layers
    box_render_null_t *null;
    ecs_world_t *world = layers_world(&null);
    ecs_entity_t snow = ecs_new(world);
    ecs_add(world, snow, TextureLayer);
    ecs_entity_t plain = layers_box(world, 0);
    ecs_entity_t covered = layers_box(world, 2);
    ecs_set(world, covered, TextureLayers, {.layers = {snow}});
    layers_frame(world);
    layers_frame(world);

    test_int(layers_batch_index(world, plain), layers_batch_index(world, covered));
    box_draw_batch_t *b = layers_batch(world, plain);
    test_bool(box_draws_key_layered(&b->key), false);
    const box_render_batch_t *draw = layers_draw(world, null, plain);
    test_bool(draw->texture_layers, false);
    test_uint(draw->instance_count, 2);

    ecs_fini(world);
    box_render_null_free(null);
}

/** In a scene with Scene.texture_layers a shape without TextureLayers keeps the batch, draw and
 * pipeline it has without the feature; shapes with the same layers share a batch of their own,
 * instances of a prefab with layers too, and other layers make another batch */
void TextureLayers_batches(void) {
    // materials/texture_layers
    box_render_null_t *null;
    ecs_world_t *world = layers_world(&null);
    ecs_singleton_set(world, Scene, {.shader_features.Extended.texture_layers = true});
    ecs_entity_t snow = ecs_new(world);
    ecs_add(world, snow, TextureLayer);
    ecs_entity_t wet = ecs_new(world);
    ecs_set(world, wet, TextureLayer, {.amount = 1, .albedo = LayerMultiply});
    ecs_entity_t plain = layers_box(world, 0);
    ecs_entity_t first = layers_box(world, 2);
    ecs_set(world, first, TextureLayers, {.layers = {snow}});
    ecs_entity_t second = layers_box(world, 4);
    ecs_set(world, second, TextureLayers, {.layers = {snow}});
    ecs_entity_t other = layers_box(world, 6);
    ecs_set(world, other, TextureLayers, {.layers = {snow, wet}});
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_set(world, prefab, TextureLayers, {.layers = {wet}});
    ecs_entity_t inst_a = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, inst_a, Position, {0, 3, 0});
    ecs_entity_t inst_b = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, inst_b, Position, {2, 3, 0});
    layers_frame(world);
    layers_frame(world);

    box_draw_batch_t *pb = layers_batch(world, plain);
    test_bool(box_draws_key_layered(&pb->key), false);
    test_int(pb->key.pipeline, BOX_RENDER_SHADER_MESH);
    const box_render_batch_t *pd = layers_draw(world, null, plain);
    test_bool(pd->texture_layers, false);
    test_uint(pd->layers, 0);
    test_uint(pd->instance_count, 1);

    test_int(layers_batch_index(world, first), layers_batch_index(world, second));
    test_assert(layers_batch_index(world, first) != layers_batch_index(world, plain));
    test_assert(layers_batch_index(world, other) != layers_batch_index(world, first));
    test_int(layers_batch_index(world, inst_a), layers_batch_index(world, inst_b));
    test_assert(layers_batch_index(world, inst_a) != layers_batch_index(world, first));
    box_draw_batch_t *fb = layers_batch(world, first);
    test_uint(fb->key.layers[0], snow);
    test_uint(fb->key.layers[1], 0);
    test_int(fb->key.pipeline, BOX_RENDER_SHADER_MESH);
    test_uint(fb->key.mesh, pb->key.mesh);

    const box_render_batch_t *fd = layers_draw(world, null, first);
    test_uint(fd->instance_count, 2);
    const box_render_texture_layers_t *r = layers_record(world, null, first);
    test_uint(r->count, 1);
    test_uint(r->entities[0], snow);
    test_uint(r->entities[1], 0);
    test_flt(r->values[0].color[3], 1);
    test_uint(r->values[0].modes[0], LayerMix);
    test_flt(r->values[1].color[3], 0);

    r = layers_record(world, null, other);
    test_uint(r->count, 2);
    test_uint(r->entities[1], wet);
    test_uint(r->values[1].modes[0], LayerMultiply);

    r = layers_record(world, null, inst_a);
    test_uint(r->entities[0], wet);
    test_uint(layers_draw(world, null, inst_a)->instance_count, 2);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Adding TextureLayers moves a shape into a layers batch, removing it moves the shape back into
 * the batch of the shapes without layers */
void TextureLayers_add_remove(void) {
    // materials/texture_layers
    box_render_null_t *null;
    ecs_world_t *world = layers_world(&null);
    ecs_singleton_set(world, Scene, {.shader_features.Extended.texture_layers = true});
    ecs_entity_t snow = ecs_new(world);
    ecs_add(world, snow, TextureLayer);
    ecs_entity_t plain = layers_box(world, 0);
    ecs_entity_t e = layers_box(world, 2);
    layers_frame(world);
    layers_frame(world);
    int32_t shared = layers_batch_index(world, plain);
    test_int(layers_batch_index(world, e), shared);
    test_uint(layers_draw(world, null, plain)->instance_count, 2);

    ecs_set(world, e, TextureLayers, {.layers = {snow}});
    layers_frame(world);
    layers_frame(world);
    test_assert(layers_batch_index(world, e) != shared);
    test_int(layers_batch_index(world, plain), shared);
    test_bool(box_draws_key_layered(&layers_batch(world, e)->key), true);
    test_bool(layers_draw(world, null, e)->texture_layers, true);
    test_uint(layers_draw(world, null, plain)->instance_count, 1);

    ecs_remove(world, e, TextureLayers);
    layers_frame(world);
    layers_frame(world);
    test_int(layers_batch_index(world, e), layers_batch_index(world, plain));
    test_bool(box_draws_key_layered(&layers_batch(world, e)->key), false);
    test_uint(layers_draw(world, null, plain)->instance_count, 2);
    test_bool(layers_draw(world, null, plain)->texture_layers, false);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Writing the amount of a layer every frame reaches the record of every draw with the layer
 * without moving the shapes to another batch or slot */
void TextureLayers_amount_animates(void) {
    // materials/texture_layers
    box_render_null_t *null;
    ecs_world_t *world = layers_world(&null);
    ecs_singleton_set(world, Scene, {.shader_features.Extended.texture_layers = true});
    ecs_entity_t snow = ecs_new(world);
    ecs_set(world, snow, TextureLayer, {.amount = 0.2f, .direction = {0, 1, 0},
        .contrast = 8, .albedo = LayerMix, .roughness = LayerMix});
    ecs_entity_t a = layers_box(world, 0);
    ecs_set(world, a, TextureLayers, {.layers = {snow}});
    ecs_entity_t b = layers_box(world, 2);
    ecs_set(world, b, Rgb, {200, 100, 50});
    ecs_set(world, b, TextureLayers, {.layers = {0, snow}});
    layers_frame(world);
    layers_frame(world);

    const DrawSlot *sa = ecs_get(world, a, DrawSlot);
    const DrawSlot *sb = ecs_get(world, b, DrawSlot);
    int32_t batch_a = sa->batch, slot_a = sa->slot;
    int32_t batch_b = sb->batch, slot_b = sb->slot;
    int32_t batches = ecs_vec_count(&box_draws_state(world)->batches);
    test_flt(layers_record(world, null, a)->values[0].color[3], 0.2f);
    test_flt(layers_record(world, null, b)->values[1].color[3], 0.2f);
    test_uint(layers_record(world, null, b)->entities[0], 0);
    test_uint(layers_record(world, null, b)->count, 2);

    int32_t i;
    for (i = 1; i <= 4; i ++) {
        TextureLayer *l = ecs_get_mut(world, snow, TextureLayer);
        l->amount = 0.2f + 0.2f * (float)i;
        ecs_modified(world, snow, TextureLayer);
        layers_frame(world);
        sa = ecs_get(world, a, DrawSlot);
        sb = ecs_get(world, b, DrawSlot);
        test_int(sa->batch, batch_a);
        test_int(sa->slot, slot_a);
        test_int(sb->batch, batch_b);
        test_int(sb->slot, slot_b);
        test_int(ecs_vec_count(&box_draws_state(world)->batches), batches);
        test_flt(layers_record(world, null, a)->values[0].color[3], 0.2f + 0.2f * (float)i);
        test_flt(layers_record(world, null, b)->values[1].color[3], 0.2f + 0.2f * (float)i);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

/** The record of a draw holds the handles of the maps of each layer and of its mask, and a
 * deleted layer entity does nothing */
void TextureLayers_record_maps(void) {
    // materials/texture_layers
    box_render_null_t *null;
    ecs_world_t *world = layers_world(&null);
    ecs_singleton_set(world, Scene, {.shader_features.Extended.texture_layers = true});
    ecs_entity_t rgb = layers_texture(world, LAYERS_RGB_PNG);
    ecs_entity_t gray = layers_texture(world, LAYERS_GRAY_PNG);
    ecs_entity_t snow = ecs_new(world);
    ecs_set(world, snow, TextureLayer, {.amount = 1, .mask = gray, .contrast = 8,
        .albedo = LayerMix, .roughness = LayerMix});
    ecs_set(world, snow, PbrTextures, {.albedo = rgb, .orm = gray});
    ecs_entity_t gone = ecs_new(world);
    ecs_add(world, gone, TextureLayer);
    ecs_entity_t e = layers_box(world, 0);
    ecs_set(world, e, TextureLayers, {.layers = {gone, snow}});
    ecs_entity_t textures[2] = { rgb, gray };
    layers_wait(world, textures, 2);

    uint32_t rgb_handle = box_textures_handle(box_textures_get(world), rgb);
    uint32_t gray_handle = box_textures_handle(box_textures_get(world), gray);
    test_assert(rgb_handle != 0);
    test_assert(gray_handle != 0);
    const box_render_texture_layers_t *r = layers_record(world, null, e);
    test_uint(r->count, 2);
    test_uint(r->maps[1][BOX_RENDER_LAYER_ALBEDO], rgb_handle);
    test_uint(r->maps[1][BOX_RENDER_LAYER_ORM], gray_handle);
    test_uint(r->maps[1][BOX_RENDER_LAYER_NORMAL], 0);
    test_uint(r->maps[1][BOX_RENDER_LAYER_MASK], gray_handle);
    test_uint(r->values[1].flags[0], BOX_RENDER_LAYER_HAS_ALBEDO | BOX_RENDER_LAYER_HAS_ORM |
        BOX_RENDER_LAYER_HAS_MASK);
    test_uint(r->entities[0], gone);

    ecs_delete(world, gone);
    layers_frame(world);
    r = layers_record(world, null, e);
    test_uint(r->entities[0], 0);
    test_flt(r->values[0].color[3], 0);
    test_uint(r->entities[1], snow);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Transmissive shapes and 2D shapes ignore their TextureLayers */
void TextureLayers_ignored(void) {
    // materials/texture_layers
    box_render_null_t *null;
    ecs_world_t *world = layers_world(&null);
    ecs_singleton_set(world, Scene, {.shader_features.Extended.texture_layers = true,
        .shader_features.Extended.transmission = true});
    ecs_entity_t snow = ecs_new(world);
    ecs_add(world, snow, TextureLayer);
    ecs_entity_t glass = layers_box(world, 0);
    ecs_set(world, glass, Transmission, {.transmission_factor = 1, .ior = 1.5f});
    ecs_set(world, glass, TextureLayers, {.layers = {snow}});
    ecs_entity_t rect = ecs_new(world);
    ecs_set(world, rect, Position, {0, 0, 0});
    ecs_set(world, rect, Rect, {.width = 1, .height = 1});
    ecs_set(world, rect, TextureLayers, {.layers = {snow}});
    layers_frame(world);
    layers_frame(world);

    test_bool(box_draws_key_layered(&layers_batch(world, glass)->key), false);
    test_bool(box_draws_key_layered(&layers_batch(world, rect)->key), false);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Find the draw of a water batch in the last packet: water batches draw per view.
 *
 * @param null The backend
 * @param layered Whether the draw has TextureLayers
 * @return The draw, NULL for none
 */
static const box_render_batch_t* layers_water_draw(
    const box_render_null_t *null,
    bool layered)
{
    const box_render_batch_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.batches); i ++) {
        const box_render_batch_t *draw = ecs_vec_get_t(
            &null->last.batches, box_render_batch_t, i);
        if (draw->water && draw->texture_layers == layered) {
            result = draw;
        }
    }

    return result;
}

/** A Water shape with TextureLayers in a scene with Scene.water and Scene.texture_layers keys
 * its layers and draws them with the layers record; a Water shape without them keeps the batch
 * and draw it has without the feature, and without Scene.water a Water shape with layers is an
 * opaque layered mesh */
void TextureLayers_water(void) {
    // materials/texture_layers
    box_render_null_t *null;
    ecs_world_t *world = layers_world(&null);
    ecs_singleton_set(world, Scene, {.shader_features.Extended.texture_layers = true,
        .shader_features.Extended.water = true});
    ecs_entity_t rain = ecs_new(world);
    ecs_set(world, rain, TextureLayer, {.amount = 1, .normal = 1});
    ecs_entity_t brook = layers_box(world, 0);
    ecs_add(world, brook, Water);
    ecs_set(world, brook, TextureLayers, {.layers = {rain}});
    ecs_entity_t pond = layers_box(world, 2);
    ecs_add(world, pond, Water);
    layers_frame(world);
    layers_frame(world);

    box_draw_batch_t *bb = layers_batch(world, brook);
    test_bool(bb->key.water, true);
    test_bool(bb->key.transmissive, true);
    test_int(bb->key.pipeline, BOX_RENDER_SHADER_MESH_EXTENDED);
    test_bool(box_draws_key_layered(&bb->key), true);
    test_uint(bb->key.layers[0], rain);

    box_draw_batch_t *pb = layers_batch(world, pond);
    test_bool(pb->key.water, true);
    test_bool(box_draws_key_layered(&pb->key), false);
    test_assert(layers_batch_index(world, pond) != layers_batch_index(world, brook));

    const box_render_batch_t *draw = layers_water_draw(null, true);
    test_assert(draw != NULL);
    test_bool(draw->transmissive, true);
    const box_render_texture_layers_t *r = box_render_packet_data(&null->last, draw->layers);
    test_uint(r->count, 1);
    test_uint(r->entities[0], rain);
    test_flt(r->values[0].color[3], 1);

    draw = layers_water_draw(null, false);
    test_assert(draw != NULL);
    test_uint(draw->layers, 0);

    ecs_singleton_set(world, Scene, {.shader_features.Extended.texture_layers = true});
    layers_frame(world);
    layers_frame(world);
    bb = layers_batch(world, brook);
    test_bool(bb->key.water, false);
    test_bool(bb->key.transmissive, false);
    test_bool(box_draws_key_layered(&bb->key), true);

    ecs_fini(world);
    box_render_null_free(null);
}
