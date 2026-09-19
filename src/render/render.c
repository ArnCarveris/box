/**
 * @file render/render.c
 * @brief Registers the box.render module, its registry singleton and mesh uploads.
 */

#define BOX_RENDER_IMPL
#include "render_internal.h"

ECS_COMPONENT_DECLARE(RenderInstance);
ECS_COMPONENT_DECLARE(RenderState);

/** Registry destructor releasing batches, pending meshes and queries.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_render_state_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    RenderState *state = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        int32_t j;
        for (j = 0; j < ecs_vec_count(&state[i].batches); j ++) {
            box_batch_t *batch = ecs_vec_get_t(&state[i].batches, box_batch_t, j);
            ecs_vec_fini_t(NULL, &batch->entities, ecs_entity_t);
            ecs_vec_fini_t(NULL, &batch->groups, box_slot_group_t);
        }

        ecs_vec_fini_t(NULL, &state[i].batches, box_batch_t);
        box_render_mesh_fini(&state[i]);
        for (j = 0; j < ecs_vec_count(&state[i].pending_textures); j ++) {
            free(ecs_vec_get_t(&state[i].pending_textures, box_texture_cmd_t, j)->pixels);
        }

        ecs_vec_fini_t(NULL, &state[i].pending_textures, box_texture_cmd_t);
        ecs_vec_fini_t(NULL, &state[i].dead_textures, uint32_t);
        for (j = 0; j < BOX_TEXTURE_PAGES; j ++) {
            ecs_vec_fini_t(NULL, &state[i].pages[j].free_layers, uint32_t);
        }

        ecs_vec_fini_t(NULL, &state[i].layers, box_layer_t);
        ecs_vec_fini_t(NULL, &state[i].view_layers, ecs_entity_t);
        ecs_vec_fini_t(NULL, &state[i].dirty, ecs_entity_t);
        ecs_vec_fini_t(NULL, &state[i].transparent, ecs_entity_t);
        ecs_vec_fini_t(NULL, &state[i].sort, box_sort_key_t);
        ecs_vec_fini_t(NULL, &state[i].sort_tmp, box_sort_key_t);
        ecs_vec_fini_t(NULL, &state[i].view_list, box_view_src_t);
        ecs_vec_fini_t(NULL, &state[i].view_env, box_view_env_t);
        ecs_vec_fini_t(NULL, &state[i].view_clip, mat4);
        ecs_vec_fini_t(NULL, &state[i].dead_targets, uint32_t);
        if (state[i].views) ecs_query_fini(state[i].views);
        if (state[i].targets) ecs_query_fini(state[i].targets);
        if (state[i].dynamic) ecs_query_fini(state[i].dynamic);
        if (state[i].cameras) ecs_query_fini(state[i].cameras);
        if (state[i].captures) ecs_query_fini(state[i].captures);
        if (state[i].suns) ecs_query_fini(state[i].suns);
        if (state[i].lights) ecs_query_fini(state[i].lights);
        if (state[i].emitters) ecs_query_fini(state[i].emitters);
        if (state[i].layer_query) ecs_query_fini(state[i].layer_query);
        if (state[i].surfaces) ecs_query_fini(state[i].surfaces);
        if (state[i].instances) ecs_query_fini(state[i].instances);
    }
}

RenderState* box_render_state(
    ecs_world_t *world)
{
    RenderState *state = ecs_get_mut(world, ecs_id(RenderState), RenderState);
    ecs_assert(state != NULL, ECS_INVALID_OPERATION, "box.render is not imported");

    return state;
}

void BoxRenderImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxRender);
    ECS_IMPORT(world, BoxCore);
    ECS_IMPORT(world, BoxSurface);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxCamera);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxLight);
    ECS_IMPORT(world, BoxMaterial);

    ECS_META_COMPONENT(world, RenderMesh);
    ECS_META_COMPONENT(world, RenderTexture);
    ECS_COMPONENT_DEFINE(world, RenderInstance);
    ECS_COMPONENT_DEFINE(world, RenderState);

    ecs_set_hooks(world, RenderState, {
        .ctor = flecs_default_ctor,
        .dtor = box_render_state_dtor
    });
    ecs_set_hooks(world, RenderInstance, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, RenderTexture, { .ctor = flecs_default_ctor });
    ecs_add_pair(world, ecs_id(RenderInstance), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(RenderMesh), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(RenderTexture), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(RenderMesh), EcsWith, ecs_id(WorldTransform));
    ecs_add_pair(world, ecs_id(RenderMesh), EcsWith, ecs_id(RenderInstance));

    RenderState *state = ecs_singleton_ensure(world, RenderState);
    ecs_vec_init_t(NULL, &state->batches, box_batch_t, 0);
    box_render_mesh_init(state);
    ecs_vec_init_t(NULL, &state->pending_textures, box_texture_cmd_t, 0);
    ecs_vec_init_t(NULL, &state->dead_textures, uint32_t, 0);
    int32_t page;
    for (page = 0; page < BOX_TEXTURE_PAGES; page ++) {
        ecs_vec_init_t(NULL, &state->pages[page].free_layers, uint32_t, 0);
    }

    ecs_vec_init_t(NULL, &state->layers, box_layer_t, 0);
    ecs_vec_init_t(NULL, &state->view_layers, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->dirty, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->transparent, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->sort, box_sort_key_t, 0);
    ecs_vec_init_t(NULL, &state->sort_tmp, box_sort_key_t, 0);
    ecs_vec_init_t(NULL, &state->view_list, box_view_src_t, 0);
    ecs_vec_init_t(NULL, &state->view_env, box_view_env_t, 0);
    ecs_vec_init_t(NULL, &state->view_clip, mat4, 0);
    ecs_vec_init_t(NULL, &state->dead_targets, uint32_t, 0);

    box_render_texture_init(world);
    box_render_view_init(world);
    box_render_layers_init(world);
    box_render_view_layers_init(world);
    box_render_instance_init(world);
    box_render_extract_init(world);
    ECS_IMPORT(world, BoxShader);
    ECS_IMPORT(world, BoxEffects);
    ECS_IMPORT(world, BoxSky);
}
