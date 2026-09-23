#include <box.h>
#include <bake_test.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/renderer/renderer_internal.h"

static ecs_world_t *world;
static box_renderer_t *renderer;
static ecs_entity_t camera;

static void render_frame(int change)
{
    box_packet_t *p = box_renderer_acquire(renderer);
    test_assert(p != NULL);
    p->width = 128;
    p->height = 128;
    box_render_extract(world, p);
    box_view_t *v = box_vec_at(&p->views, 0);
    switch (change) {
    case 1: v->view[12] += 0.25f; break;
    case 2: v->proj[0] *= 0.9f; break;
    case 3: v->cluster.near_ *= 1.1f; break;
    case 4: v->cluster.far_ *= 1.1f; break;
    case 5: v->cluster.max_lights ++; break;
    case 6: v->light_count = 0; break;
    case 7: ((box_light_t*)box_vec_at(&p->lights, v->light_first))->position[0] += 0.5f; break;
    case 8: ((box_light_t*)box_vec_at(&p->lights, v->light_first))->is_static = 0; break;
    case 9: ((box_light_t*)box_vec_at(&p->lights, v->light_first))->range *= 0.5f; break;
    }
    box_renderer_submit(renderer, p);
    test_int(box_renderer_errors(renderer), 0);
}

void RenderCache_setup(void)
{
    world = ecs_init();
    ECS_IMPORT(world, Box);
    camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, -8});
    ecs_set(world, camera, LookAt, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .enabled = true});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {0, 2, -2});
    ecs_set(world, light, PointLight, {.intensity = 2, .range = 10});
    renderer = box_renderer_create(&(box_renderer_desc_t){.width = 128, .height = 128});
    test_assert(renderer != NULL);
    render_frame(0);
    render_frame(0);
}

void RenderCache_teardown(void)
{
    box_renderer_destroy(renderer);
    ecs_fini(world);
}

void RenderCache_clusters_reuse_unchanged_inputs(void)
{
    render_frame(0);
    test_bool(renderer->v->cluster_dirty, false);
    test_int(renderer->v->light_snapshot.count, 1);
}

void RenderCache_clusters_invalidate_changed_inputs(void)
{
    for (int change = 1; change <= 9; change ++) {
        render_frame(change);
        test_bool(renderer->v->cluster_dirty, true);
        render_frame(change);
        test_bool(renderer->v->cluster_dirty, false);
        render_frame(0);
        test_bool(renderer->v->cluster_dirty, true);
        render_frame(0);
        test_bool(renderer->v->cluster_dirty, false);
    }
}

void RenderCache_clusters_keep_views_separate(void)
{
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .enabled = true});
    render_frame(0);
    render_frame(0);
    test_int(renderer->views.count, 2);
    render_frame(1);
    box_gpu_view_t *first = box_vec_at(&renderer->views, 0);
    box_gpu_view_t *second = box_vec_at(&renderer->views, 1);
    test_bool(first->cluster_dirty, true);
    test_bool(second->cluster_dirty, false);
    test_assert(first->light_snapshot.data != second->light_snapshot.data);
    render_frame(1);
    test_bool(first->cluster_dirty, false);
    test_bool(second->cluster_dirty, false);
}

void RenderCache_solid_slots_follow_material_changes(void)
{
    box_gpu_mesh_t *mesh = box_vec_at(&renderer->meshes, 0);
    test_int(mesh->cutout_count, 0);
    box_instance_t instance = *(box_instance_t*)box_vec_at(&mesh->mirror, 0);
    instance.textures[0] = 1;
    box_scene_mirror(mesh, 0, &instance);
    test_int(mesh->cutout_count, 1);
    box_scene_mirror(mesh, 0, &instance);
    test_int(mesh->cutout_count, 1);
    instance.textures[0] = 0;
    instance.uv_mode |= 1u << BOX_MODE_CLIP_SHIFT;
    box_scene_mirror(mesh, 0, &instance);
    test_int(mesh->cutout_count, 1);
    instance.uv_mode &= ~(1u << BOX_MODE_CLIP_SHIFT);
    instance.uv_mode |= 1u << BOX_MODE_SHAPE_SHIFT;
    box_scene_mirror(mesh, 0, &instance);
    test_int(mesh->cutout_count, 1);
    instance.uv_mode &= ~(1u << BOX_MODE_SHAPE_SHIFT);
    box_scene_mirror(mesh, 0, &instance);
    test_int(mesh->cutout_count, 0);
}

void RenderCache_solid_slots_survive_mirror_limit_and_removal(void)
{
    box_gpu_mesh_t *mesh = box_vec_at(&renderer->meshes, 0);
    box_instance_t instance = *(box_instance_t*)box_vec_at(&mesh->mirror, 0);
    instance.textures[0] = 1;
    box_scene_mirror(mesh, BOX_CULL_MIRROR_MAX, &instance);
    test_bool(mesh->mirror_valid, false);
    test_int(mesh->cutout_count, 1);
    box_packet_t p;
    box_packet_init(&p);
    box_static_count_t *count = box_vec_push(&p.static_counts);
    *count = (box_static_count_t){.mesh = 0, .count = 1};
    box_scene_apply(renderer, &p);
    test_int(mesh->cutout_count, 0);
    test_int(mesh->cutout_slots.count, 1);
    box_packet_fini(&p);
}
