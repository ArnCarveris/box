/**
 * @file MeshShader.c
 * @brief Tests for the split of the mesh shaders and the Scene features of their pipelines: which
 * shapes draw with the extended mesh shader, how a shape falls back when its scene lacks a
 * feature, which batch it moves to when its material or scene changes, and the features of a
 * view.
 *
 * Every case renders with the null backend and reads the batch keys of the extract or the views
 * of the last packet.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "draw.h"

void MeshShader_plain_mesh_draws_with_mesh_shader(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.alpha_cutout = true, .blend_modes = true,
        .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
        .clear_coat = true, .anisotropy = true, .diffuse_transmission = true});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Position, {0, 0, 0});
    ecs_set(world, plain, Box, {1, 1, 1});
    ecs_set(world, plain, Rgb, {200, 100, 50});
    ecs_set(world, plain, PbrMaterial, {.metallic = 0.5f, .roughness = 0.25f});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *plain_slot = ecs_get(world, plain, DrawSlot);
    test_assert(plain_slot != NULL && plain_slot->batch >= 0);
    test_int(box_draws_batch(d, plain_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_sheen_draws_with_extended(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.alpha_cutout = true, .blend_modes = true,
        .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
        .clear_coat = true, .anisotropy = true, .diffuse_transmission = true});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Position, {0, 0, 0});
    ecs_set(world, plain, Box, {1, 1, 1});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {2, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_set(world, shape, Sheen, {.color = {255, 0, 128}, .roughness = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH_EXTENDED);
    const DrawSlot *plain_slot = ecs_get(world, plain, DrawSlot);
    test_assert(plain_slot != NULL && plain_slot->batch >= 0);
    test_int(box_draws_batch(d, plain_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_clear_coat_draws_with_extended(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.alpha_cutout = true, .blend_modes = true,
        .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
        .clear_coat = true, .anisotropy = true, .diffuse_transmission = true});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Position, {0, 0, 0});
    ecs_set(world, plain, Box, {1, 1, 1});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {2, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_set(world, shape, ClearCoat, {.strength = 1, .roughness = 0.1f});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH_EXTENDED);
    const DrawSlot *plain_slot = ecs_get(world, plain, DrawSlot);
    test_assert(plain_slot != NULL && plain_slot->batch >= 0);
    test_int(box_draws_batch(d, plain_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_specular_anisotropy_draws_with_extended(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.alpha_cutout = true, .blend_modes = true,
        .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
        .clear_coat = true, .anisotropy = true, .diffuse_transmission = true});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Position, {0, 0, 0});
    ecs_set(world, plain, Box, {1, 1, 1});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {2, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_set(world, shape, SpecularAnisotropy, {.strength = 0.5f, .rotation = 0});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH_EXTENDED);
    const DrawSlot *plain_slot = ecs_get(world, plain, DrawSlot);
    test_assert(plain_slot != NULL && plain_slot->batch >= 0);
    test_int(box_draws_batch(d, plain_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_diffuse_transmission_draws_with_extended(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.alpha_cutout = true, .blend_modes = true,
        .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
        .clear_coat = true, .anisotropy = true, .diffuse_transmission = true});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Position, {0, 0, 0});
    ecs_set(world, plain, Box, {1, 1, 1});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {2, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_set(world, shape, DiffuseTransmission, {.factor = 0.5f, .color = {255, 255, 255}});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH_EXTENDED);
    const DrawSlot *plain_slot = ecs_get(world, plain, DrawSlot);
    test_assert(plain_slot != NULL && plain_slot->batch >= 0);
    test_int(box_draws_batch(d, plain_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_sheen_off_draws_with_mesh_shader(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.clear_coat = true});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_set(world, shape, Sheen, {.color = {255, 0, 128}, .roughness = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_clear_coat_off_draws_with_mesh_shader(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.sheen = true});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_set(world, shape, ClearCoat, {.strength = 1, .roughness = 0.1f});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_specular_anisotropy_off_draws_with_mesh_shader(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.sheen = true});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_set(world, shape, SpecularAnisotropy, {.strength = 0.5f, .rotation = 0});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_diffuse_transmission_off_draws_with_mesh_shader(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.sheen = true});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_set(world, shape, DiffuseTransmission, {.factor = 0.5f, .color = {255, 255, 255}});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_one_enabled_feature_draws_with_extended(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.clear_coat = true});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_set(world, shape, Sheen, {.color = {255, 255, 255}, .roughness = 0.5f});
    ecs_set(world, shape, ClearCoat, {.strength = 1, .roughness = 0.1f});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH_EXTENDED);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_no_scene_features_draws_with_mesh_shader(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_set(world, shape, Sheen, {.color = {255, 255, 255}, .roughness = 0.5f});
    ecs_set(world, shape, AlphaCutoff, {0.5f});
    ecs_set(world, shape, Opacity, {0.5f});
    ecs_set(world, shape, Transmission, {.transmission_factor = 1, .ior = 1.5f});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    box_draw_batch_t *b = box_draws_batch(d, shape_slot->batch);
    test_int(b->key.pipeline, BOX_RENDER_SHADER_MESH);
    test_int(b->key.blend, Opaque);
    test_bool(b->key.cutout, false);
    test_bool(b->key.transmissive, false);
    test_uint(b->key.features, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_alpha_cutoff_off_draws_opaque(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.sheen = true});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_set(world, shape, AlphaCutoff, {0.5f});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    box_draw_batch_t *b = box_draws_batch(d, shape_slot->batch);
    test_int(b->key.pipeline, BOX_RENDER_SHADER_MESH);
    test_bool(b->key.cutout, false);
    test_int(b->key.blend, Opaque);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_blend_modes_off_draw_opaque(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.sheen = true});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_entity_t faded = ecs_new(world);
    ecs_set(world, faded, Position, {2, 0, 0});
    ecs_set(world, faded, Box, {1, 1, 1});
    ecs_set(world, shape, BlendMode, {Additive});
    ecs_set(world, faded, Opacity, {0.5f});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    box_draw_batch_t *b = box_draws_batch(d, shape_slot->batch);
    test_int(b->key.pipeline, BOX_RENDER_SHADER_MESH);
    test_int(b->key.blend, Opaque);
    const DrawSlot *faded_slot = ecs_get(world, faded, DrawSlot);
    test_assert(faded_slot != NULL && faded_slot->batch >= 0);
    b = box_draws_batch(d, faded_slot->batch);
    test_int(b->key.pipeline, BOX_RENDER_SHADER_MESH);
    test_int(b->key.blend, Opaque);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_transmission_draws_with_extended(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.alpha_cutout = true, .blend_modes = true,
        .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
        .clear_coat = true, .anisotropy = true, .diffuse_transmission = true});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_set(world, shape, Transmission, {.transmission_factor = 1, .ior = 1.5f});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    box_draw_batch_t *b = box_draws_batch(d, shape_slot->batch);
    test_int(b->key.pipeline, BOX_RENDER_SHADER_MESH_EXTENDED);
    test_bool(b->key.transmissive, true);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_transmission_off_draws_with_mesh_shader(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.sheen = true});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_set(world, shape, Transmission, {.transmission_factor = 1, .ior = 1.5f});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    box_draw_batch_t *b = box_draws_batch(d, shape_slot->batch);
    test_int(b->key.pipeline, BOX_RENDER_SHADER_MESH);
    test_bool(b->key.transmissive, false);
    test_bool(b->key.shadow, true);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_triplanar_mapping_draws_with_extended(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.alpha_cutout = true, .blend_modes = true,
        .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
        .clear_coat = true, .anisotropy = true, .diffuse_transmission = true});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_set(world, shape, TextureMapping, {.mode = MappingWorld, .size = 1, .blend = 0.5f});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH_EXTENDED);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_triplanar_off_draws_with_mesh_shader(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.sheen = true});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_set(world, shape, TextureMapping, {.mode = MappingWorld, .size = 1, .blend = 0.5f});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_planar_mapping_draws_with_mesh_shader(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.alpha_cutout = true, .blend_modes = true,
        .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
        .clear_coat = true, .anisotropy = true, .diffuse_transmission = true});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_set(world, shape, TextureMapping, {.mode = MappingWorld, .size = 1, .blend = 0});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_mapping_blend_change_moves_shape(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.alpha_cutout = true, .blend_modes = true,
        .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
        .clear_coat = true, .anisotropy = true, .diffuse_transmission = true});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_set(world, shape, TextureMapping, {.mode = MappingObject, .size = 1, .blend = 0});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);

    ecs_set(world, shape, TextureMapping, {.mode = MappingObject, .size = 1, .blend = 0.25f});
    ecs_progress(world, 0);
    box_render_sync(world);

    shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH_EXTENDED);

    ecs_set(world, shape, TextureMapping, {.mode = MappingObject, .size = 1, .blend = 0});
    ecs_progress(world, 0);
    box_render_sync(world);

    shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_add_sheen_moves_to_extended(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.alpha_cutout = true, .blend_modes = true,
        .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
        .clear_coat = true, .anisotropy = true, .diffuse_transmission = true});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);

    ecs_set(world, shape, Sheen, {.color = {255, 255, 255}, .roughness = 0.5f});
    ecs_progress(world, 0);
    box_render_sync(world);

    shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH_EXTENDED);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_remove_clear_coat_moves_to_mesh_shader(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.alpha_cutout = true, .blend_modes = true,
        .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
        .clear_coat = true, .anisotropy = true, .diffuse_transmission = true});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_set(world, shape, ClearCoat, {.strength = 1, .roughness = 0.1f});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH_EXTENDED);

    ecs_remove(world, shape, ClearCoat);
    ecs_progress(world, 0);
    box_render_sync(world);

    shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_inherited_sheen_draws_with_extended(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.alpha_cutout = true, .blend_modes = true,
        .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
        .clear_coat = true, .anisotropy = true, .diffuse_transmission = true});
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_set(world, prefab, Sheen, {.color = {255, 255, 255}, .roughness = 0.5f});
    ecs_entity_t shape = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH_EXTENDED);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_prefab_anisotropy_change_moves_instances(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.alpha_cutout = true, .blend_modes = true,
        .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
        .clear_coat = true, .anisotropy = true, .diffuse_transmission = true});
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_entity_t a = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_entity_t b = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, b, Position, {2, 0, 0});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *a_slot = ecs_get(world, a, DrawSlot);
    test_assert(a_slot != NULL && a_slot->batch >= 0);
    test_int(box_draws_batch(d, a_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);
    const DrawSlot *b_slot = ecs_get(world, b, DrawSlot);
    test_assert(b_slot != NULL && b_slot->batch >= 0);
    test_int(box_draws_batch(d, b_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);

    ecs_set(world, prefab, SpecularAnisotropy, {.strength = 0.5f, .rotation = 0});
    ecs_progress(world, 0);
    box_render_sync(world);

    a_slot = ecs_get(world, a, DrawSlot);
    test_assert(a_slot != NULL && a_slot->batch >= 0);
    test_int(box_draws_batch(d, a_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH_EXTENDED);
    b_slot = ecs_get(world, b, DrawSlot);
    test_assert(b_slot != NULL && b_slot->batch >= 0);
    test_int(box_draws_batch(d, b_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH_EXTENDED);

    ecs_remove(world, prefab, SpecularAnisotropy);
    ecs_progress(world, 0);
    box_render_sync(world);

    a_slot = ecs_get(world, a, DrawSlot);
    test_assert(a_slot != NULL && a_slot->batch >= 0);
    test_int(box_draws_batch(d, a_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);
    b_slot = ecs_get(world, b, DrawSlot);
    test_assert(b_slot != NULL && b_slot->batch >= 0);
    test_int(box_draws_batch(d, b_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_scene_features_change_moves_shapes(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_singleton_set(world, Scene, {.clear_coat = true});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Position, {2, 0, 0});
    ecs_set(world, plain, Box, {1, 1, 1});
    ecs_set(world, shape, Sheen, {.color = {255, 255, 255}, .roughness = 0.5f});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);
    const DrawSlot *plain_slot = ecs_get(world, plain, DrawSlot);
    test_assert(plain_slot != NULL && plain_slot->batch >= 0);
    test_int(box_draws_batch(d, plain_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);
    uint32_t before = box_draws_batch(d, plain_slot->batch)->key.features;

    ecs_singleton_set(world, Scene, {.clear_coat = true, .sheen = true, .baked = true});
    ecs_progress(world, 0);
    box_render_sync(world);

    shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH_EXTENDED);
    plain_slot = ecs_get(world, plain, DrawSlot);
    test_assert(plain_slot != NULL && plain_slot->batch >= 0);
    test_int(box_draws_batch(d, plain_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);
    test_assert(box_draws_batch(d, plain_slot->batch)->key.features != before);
    test_uint(box_draws_batch(d, plain_slot->batch)->key.features, BOX_RENDER_FEATURE_BAKED |
        BOX_RENDER_FEATURE_SHEEN | BOX_RENDER_FEATURE_CLEAR_COAT);

    ecs_singleton_set(world, Scene, {.clear_coat = true});
    ecs_progress(world, 0);
    box_render_sync(world);

    shape_slot = ecs_get(world, shape, DrawSlot);
    test_assert(shape_slot != NULL && shape_slot->batch >= 0);
    test_int(box_draws_batch(d, shape_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_scene_root_features(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    ecs_entity_t scene = ecs_new(world);
    ecs_set(world, scene, Scene, {.sheen = true});
    ecs_entity_t inside = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, inside, Position, {0, 0, 0});
    ecs_set(world, inside, Box, {1, 1, 1});
    ecs_entity_t outside = ecs_new(world);
    ecs_set(world, outside, Position, {2, 0, 0});
    ecs_set(world, outside, Box, {1, 1, 1});
    ecs_set(world, inside, Sheen, {.color = {255, 255, 255}, .roughness = 0.5f});
    ecs_set(world, outside, Sheen, {.color = {255, 255, 255}, .roughness = 0.5f});
    ecs_progress(world, 0);
    box_render_sync(world);

    const DrawSlot *inside_slot = ecs_get(world, inside, DrawSlot);
    test_assert(inside_slot != NULL && inside_slot->batch >= 0);
    test_int(box_draws_batch(d, inside_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH_EXTENDED);
    const DrawSlot *outside_slot = ecs_get(world, outside, DrawSlot);
    test_assert(outside_slot != NULL && outside_slot->batch >= 0);
    test_int(box_draws_batch(d, outside_slot->batch)->key.pipeline, BOX_RENDER_SHADER_MESH);
    test_uint(box_draws_batch(d, inside_slot->batch)->key.features, BOX_RENDER_FEATURE_SHEEN);
    test_uint(box_draws_batch(d, outside_slot->batch)->key.features, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_view_features_default(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    ecs_get_mut(world, view, View)->camera = camera;
    ecs_modified(world, view, View);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.views), 1);
    const box_render_view_t *v = ecs_vec_first(&null->last.views);
    test_uint(v->features, BOX_RENDER_FEATURE_DEPTH_PREPASS);
    test_uint(v->scene_features, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_view_features_literal(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.views), 1);
    const box_render_view_t *v = ecs_vec_first(&null->last.views);
    test_uint(v->features, 0);
    test_uint(v->scene_features, BOX_RENDER_FEATURE_BAKED | BOX_RENDER_FEATURE_REFLECTIONS);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_view_features_from_scene(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true,
        .punctual_shadows = true, .cookies = true, .environment = true,
        .aerial_perspective = true, .sheen = true});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    ecs_get_mut(world, view, View)->camera = camera;
    ecs_modified(world, view, View);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.views), 1);
    const box_render_view_t *v = ecs_vec_first(&null->last.views);
    uint32_t scene = BOX_RENDER_FEATURE_BAKED | BOX_RENDER_FEATURE_REFLECTIONS |
        BOX_RENDER_FEATURE_PUNCTUAL_SHADOWS | BOX_RENDER_FEATURE_COOKIES |
        BOX_RENDER_FEATURE_ENVIRONMENT | BOX_RENDER_FEATURE_AERIAL | BOX_RENDER_FEATURE_SHEEN;
    test_uint(v->scene_features, scene);
    test_uint(v->features, scene | BOX_RENDER_FEATURE_DEPTH_PREPASS);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_view_opts_out_of_scene_features(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true,
        .aerial_perspective = true, .cookies = true});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *value = ecs_get_mut(world, view, View);
    value->camera = camera;
    value->lighting.baked = false;
    value->lighting.reflections = false;
    value->lighting.atmosphere = false;
    value->lighting.depth_prepass = false;
    ecs_modified(world, view, View);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.views), 1);
    const box_render_view_t *v = ecs_vec_first(&null->last.views);
    test_uint(v->features, BOX_RENDER_FEATURE_COOKIES);
    test_uint(v->scene_features, BOX_RENDER_FEATURE_BAKED | BOX_RENDER_FEATURE_REFLECTIONS |
        BOX_RENDER_FEATURE_AERIAL | BOX_RENDER_FEATURE_COOKIES);

    ecs_fini(world);
    box_render_null_free(null);
}

void MeshShader_scene_features_script(void) {
    // render/mesh_shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    test_assert(ecs_script_run(world, "features",
        "using box.view\n"
        "garden { Scene: {baked: true, alpha_cutout: true} }\n"
        "view { View: {lighting: {depth_prepass: false}} }\n", NULL) == 0);
    const Scene *f = ecs_get(world, ecs_lookup(world, "garden"), Scene);
    test_assert(f != NULL);
    test_bool(f->baked, true);
    test_bool(f->alpha_cutout, true);
    test_bool(f->reflections, false);
    test_bool(f->sheen, false);
    const View *view = ecs_get(world, ecs_lookup(world, "view"), View);
    test_assert(view != NULL);
    test_bool(view->lighting.depth_prepass, false);
    test_bool(view->lighting.baked, true);
    test_bool(view->lighting.reflections, true);
    (void)camera;

    ecs_fini(world);
    box_render_null_free(null);
}
